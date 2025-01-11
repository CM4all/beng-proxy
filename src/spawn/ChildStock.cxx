// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#include "ChildStock.hxx"
#include "ChildStockItem.hxx"
#include "spawn/Interface.hxx"
#include "net/UniqueSocketDescriptor.hxx"
#include "io/UniqueFileDescriptor.hxx"
#include "util/Cancellable.hxx"

#include <cassert>

std::string_view
ChildStockClass::GetChildTag(const void *) const noexcept
{
	return {};
}

std::unique_ptr<ChildStockItem>
ChildStockClass::CreateChild(CreateStockItem c, const void *info,
			     ChildStock &child_stock)
{
	return std::make_unique<ChildStockItem>(c, child_stock,
						GetChildTag(info));
}

ChildStock::ChildStock(SpawnService &_spawn_service,
		       ListenStreamStock *_listen_stream_stock,
		       ChildStockClass &_cls,
		       Net::Log::Sink *_log_sink,
		       const ChildErrorLogOptions &_log_options) noexcept
	:spawn_service(_spawn_service),
	 listen_stream_stock(_listen_stream_stock),
	 cls(_cls),
	 log_sink(_log_sink),
	 log_options(_log_options) {}

ChildStock::~ChildStock() noexcept = default;

/**
 * An object waiting for SpawnService::Enqueue() to finish.  This
 * throttles SpawnService::SpawnChildProcess() calls if the spawner is
 * under heavy pressure.
 */
class ChildStock::QueueItem final : Cancellable {
	ChildStock &stock;
	CreateStockItem create;
	StockRequest request;
	StockGetHandler &handler;
	CancellablePointer &caller_cancel_ptr;

	CancellablePointer cancel_ptr;

public:
	QueueItem(ChildStock &_stock,
		  CreateStockItem &&_create,
		  StockRequest &&_request,
		  StockGetHandler &_handler,
		  CancellablePointer &_caller_cancel_ptr) noexcept
		:stock(_stock),
		 create(std::move(_create)), request(std::move(_request)),
		 handler(_handler), caller_cancel_ptr(_caller_cancel_ptr)
	{
	}

	void Start(SpawnService &spawner) noexcept {
		caller_cancel_ptr = *this;
		spawner.Enqueue(BIND_THIS_METHOD(OnSpawnerReady), cancel_ptr);
	}

private:
	void OnSpawnerReady() noexcept;

	void Cancel() noexcept override {
		cancel_ptr.Cancel();
		delete this;
	}
};

inline void
ChildStock::QueueItem::OnSpawnerReady() noexcept
{
	stock.DoSpawn(create, std::move(request), handler, caller_cancel_ptr);
	delete this;
}

inline void
ChildStock::DoSpawn(CreateStockItem c, StockRequest request,
		    StockGetHandler &handler,
		    CancellablePointer &caller_cancel_ptr) noexcept
try {
	auto item = cls.CreateChild(c, request.get(), *this);
	item->Spawn(cls, request.get(),
		    log_sink, log_options);

	item.release()->RegisterCompletionHandler(handler, caller_cancel_ptr);
} catch (...) {
	c.InvokeCreateError(handler, std::current_exception());
}

/*
 * stock class
 *
 */

void
ChildStock::Create(CreateStockItem c, StockRequest request,
		   StockGetHandler &handler,
		   CancellablePointer &cancel_ptr)
{
	auto *queue_item = new QueueItem(*this, std::move(c),
					 cls.PreserveRequest(std::move(request)),
					 handler, cancel_ptr);
	queue_item->Start(spawn_service);
}

/*
 * interface
 *
 */

ChildStockMap::ChildStockMap(EventLoop &event_loop, SpawnService &_spawn_service,
			     ListenStreamStock *_listen_stream_stock,
			     ChildStockMapClass &_cls,
			     Net::Log::Sink *_log_sink,
			     const ChildErrorLogOptions &_log_options,
			     unsigned _limit, unsigned _max_idle) noexcept
	:cls(_spawn_service, _listen_stream_stock,
	     _cls, _log_sink, _log_options),
	 map(event_loop, cls, _cls, _limit, _max_idle)
{
}

void
ChildStockMap::FadeTag(std::string_view tag) noexcept
{
	map.FadeIf([tag](const StockItem &_item) {
		const auto &item = (const ChildStockItem &)_item;
		return item.IsTag(tag);
	});
}

void
ChildStock::AddIdle(ChildStockItem &item) noexcept
{
	idle.push_back(item);
}

bool
ChildStock::DiscardOldestIdle() noexcept
{
	if (idle.empty())
		return false;

	/* the list front is the oldest item (the one that hasn't been
	   used for the longest time) */
	auto &item = idle.front();
	assert(item.is_idle);
	item.InvokeIdleDisconnect();

	return true;
}
