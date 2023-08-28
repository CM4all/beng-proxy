// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#pragma once

#include "Service.hxx"
#include "stock/Stock.hxx"
#include "stock/Class.hxx"
#include "net/AllocatedSocketAddress.hxx"

struct TranslateRequest;
class TranslateHandler;

class TranslationStock final : public TranslationService, StockClass {
	class Connection;
	class Request;

	Stock stock;

	const AllocatedSocketAddress address;

public:
	TranslationStock(EventLoop &event_loop, SocketAddress _address,
			 unsigned limit) noexcept
		:stock(event_loop, *this, "translation", limit, 8,
		       Event::Duration::zero()),
		 address(_address)
	{
	}

	auto &GetEventLoop() const noexcept {
		return stock.GetEventLoop();
	}

	void Get(StockGetHandler &handler,
		 CancellablePointer &cancel_ptr) noexcept {
		stock.Get(nullptr, handler, cancel_ptr);
	}

	void Put(StockItem &item, PutAction action) noexcept {
		stock.Put(item, action);
	}

	/* virtual methods from class TranslationService */
	void SendRequest(AllocatorPtr alloc,
			 const TranslateRequest &request,
			 const StopwatchPtr &parent_stopwatch,
			 TranslateHandler &handler,
			 CancellablePointer &cancel_ptr) noexcept override;

private:
	/* virtual methods from class StockClass */
	void Create(CreateStockItem c, StockRequest request,
		    StockGetHandler &handler,
		    CancellablePointer &) override;
};
