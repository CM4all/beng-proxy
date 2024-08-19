// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#pragma once

#include "stock/Stock.hxx"
#include "stock/Class.hxx"
#include "net/AllocatedSocketAddress.hxx"

class SocketDescriptor;

class TranslationStock final : StockClass {
	class Connection;

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

	PutAction Put(StockItem &item, PutAction action) noexcept {
		return stock.Put(item, action);
	}

	[[gnu::pure]]
	static SocketDescriptor GetSocket(const StockItem &item) noexcept;

private:
	/* virtual methods from class StockClass */
	void Create(CreateStockItem c, StockRequest request,
		    StockGetHandler &handler,
		    CancellablePointer &) override;
};
