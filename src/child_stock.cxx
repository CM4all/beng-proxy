/*
 * Copyright 2007-2021 CM4all GmbH
 * All rights reserved.
 *
 * author: Max Kellermann <mk@cm4all.com>
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * - Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 *
 * - Redistributions in binary form must reproduce the above copyright
 * notice, this list of conditions and the following disclaimer in the
 * documentation and/or other materials provided with the
 * distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE
 * FOUNDATION OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED
 * OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "child_stock.hxx"
#include "spawn/ExitListener.hxx"
#include "access_log/ChildErrorLog.hxx"
#include "stock/Stock.hxx"
#include "stock/Item.hxx"
#include "spawn/Interface.hxx"
#include "spawn/Prepared.hxx"
#include "system/Error.hxx"
#include "net/EasyMessage.hxx"
#include "net/TempListener.hxx"
#include "net/UniqueSocketDescriptor.hxx"
#include "io/UniqueFileDescriptor.hxx"

#include <string>

#include <assert.h>
#include <unistd.h>

int
ChildStockClass::GetChildSocketType(void *) const noexcept
{
	return SOCK_STREAM;
}

unsigned
ChildStockClass::GetChildBacklog(void *) const noexcept
{
	return 0;
}

const char *
ChildStockClass::GetChildTag(void *) const noexcept
{
	return nullptr;
}

class ChildStockItem final
	: public StockItem,
	  public ChildStockItemHook,
	  ExitListener
{
	ChildStock &child_stock;
	SpawnService &spawn_service;

	const std::string tag;

	ChildErrorLog log;

	UniqueFileDescriptor stderr_fd;

	TempListener socket;
	int pid = -1;

	bool busy = true;

public:
	ChildStockItem(CreateStockItem c,
		       ChildStock &_child_stock,
		       SpawnService &_spawn_service,
		       const char *_tag) noexcept
		:StockItem(c),
		 child_stock(_child_stock),
		 spawn_service(_spawn_service),
		 tag(_tag != nullptr ? _tag : "") {}

	~ChildStockItem() override;

	EventLoop &GetEventLoop() {
		return stock.GetEventLoop();
	}

	void Spawn(ChildStockClass &cls, void *info,
		   unsigned backlog,
		   SocketDescriptor log_socket,
		   const ChildErrorLogOptions &log_options);

	gcc_pure
	const char *GetTag() const {
		return tag.empty() ? nullptr : tag.c_str();
	}

	gcc_pure
	bool IsTag(const char *_tag) const {
		return tag == _tag;
	}

	UniqueFileDescriptor GetStderr() const noexcept {
		return stderr_fd.IsDefined()
			? UniqueFileDescriptor(dup(stderr_fd.Get()))
			: UniqueFileDescriptor{};
	}

	void SetSite(const char *site) noexcept {
		log.SetSite(site);
	}

	void SetUri(const char *uri) noexcept {
		log.SetUri(uri);
	}

	UniqueSocketDescriptor Connect() {
		try {
			return socket.Connect();
		} catch (...) {
			/* if the connection fails, abandon the child process, don't
			   try again - it will never work! */
			fade = true;
			throw;
		}
	}

	/* virtual methods from class StockItem */
	bool Borrow() noexcept override {
		assert(!busy);
		busy = true;

		/* remove from ChildStock::idle list */
		assert(ChildStockItemHook::is_linked());
		ChildStockItemHook::unlink();

		return true;
	}

	bool Release() noexcept override {
		assert(busy);
		busy = false;

		/* reuse this item only if the child process hasn't exited */
		if (pid <= 0)
			return false;

		assert(!ChildStockItemHook::is_linked());
		child_stock.AddIdle(*this);

		return true;
	}

private:
	/* virtual methods from class ExitListener */
	void OnChildProcessExit(int status) noexcept override;
};

void
ChildStockItem::Spawn(ChildStockClass &cls, void *info,
		      unsigned backlog,
		      SocketDescriptor log_socket,
		      const ChildErrorLogOptions &log_options)
{
	int socket_type = cls.GetChildSocketType(info);

	backlog = std::max(backlog, cls.GetChildBacklog(info));

	PreparedChildProcess p;
	cls.PrepareChild(info, socket.Create(socket_type, backlog), p);

	if (log_socket.IsDefined() && p.stderr_fd < 0 &&
	    p.stderr_path == nullptr)
		log.EnableClient(p, GetEventLoop(), log_socket, log_options,
				 cls.WantStderrPond(info));

	UniqueSocketDescriptor stderr_socket1, stderr_socket2;
	if (cls.WantReturnStderr(info) &&
	    !UniqueSocketDescriptor::CreateSocketPair(AF_LOCAL, SOCK_SEQPACKET, 0,
						      stderr_socket1, stderr_socket2))
		throw MakeErrno("socketpair() failed");

	pid = spawn_service.SpawnChildProcess(GetStockName(), std::move(p),
					      stderr_socket2,
					      this);

	if (stderr_socket1.IsDefined()) {
		stderr_socket2.Close();
		stderr_fd = EasyReceiveMessageWithOneFD(stderr_socket1);
	}
}

void
ChildStockItem::OnChildProcessExit(gcc_unused int status) noexcept
{
	pid = -1;

	if (!busy)
		InvokeIdleDisconnect();
}

/*
 * stock class
 *
 */

void
ChildStock::Create(CreateStockItem c, StockRequest request,
		   CancellablePointer &)
{
	auto *item = new ChildStockItem(c, *this, spawn_service,
					cls.GetChildTag(request.get()));

	try {
		item->Spawn(cls, request.get(), backlog,
			    log_socket, log_options);
	} catch (...) {
		delete item;
		throw;
	}

	item->InvokeCreateSuccess();
}

ChildStockItem::~ChildStockItem()
{
	if (pid >= 0)
		spawn_service.KillChildProcess(pid);
}

/*
 * interface
 *
 */

ChildStock::ChildStock(EventLoop &event_loop, SpawnService &_spawn_service,
		       ChildStockClass &_cls,
		       unsigned _backlog,
		       SocketDescriptor _log_socket,
		       const ChildErrorLogOptions &_log_options,
		       unsigned _limit, unsigned _max_idle) noexcept
	:map(event_loop, *this, _cls, _limit, _max_idle),
	 spawn_service(_spawn_service), cls(_cls),
	 backlog(_backlog),
	 log_socket(_log_socket),
	 log_options(_log_options)
{
}

ChildStock::~ChildStock() noexcept = default;

void
ChildStock::FadeTag(const char *tag)
{
	map.FadeIf([tag](const StockItem &_item) {
		const auto &item = (const ChildStockItem &)_item;
		return item.IsTag(tag);
	});
}

inline void
ChildStock::AddIdle(ChildStockItem &item) noexcept
{
	idle.push_back(item);
}

void
ChildStock::DiscardOldestIdle() noexcept
{
	if (idle.empty())
		return;

	/* the list front is the oldest item (the one that hasn't been
	   used for the longest time) */
	auto &item = idle.front();
	assert(item.is_idle);
	item.InvokeIdleDisconnect();
}

UniqueSocketDescriptor
child_stock_item_connect(StockItem &_item)
{
	auto &item = (ChildStockItem &)_item;

	return item.Connect();
}

const char *
child_stock_item_get_tag(const StockItem &_item)
{
	const auto &item = (const ChildStockItem &)_item;

	return item.GetTag();
}

UniqueFileDescriptor
child_stock_item_get_stderr(const StockItem &_item) noexcept
{
	const auto &item = (const ChildStockItem &)_item;

	return item.GetStderr();
}

void
child_stock_item_set_site(StockItem &_item, const char *site) noexcept
{
	auto &item = (ChildStockItem &)_item;
	item.SetSite(site);
}

void
child_stock_item_set_uri(StockItem &_item, const char *uri) noexcept
{
	auto &item = (ChildStockItem &)_item;
	item.SetUri(uri);
}
