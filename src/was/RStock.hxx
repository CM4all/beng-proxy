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

#pragma once

#include "stock/Class.hxx"
#include "stock/MultiStock.hxx"

class AllocatorPtr;
class SocketAddress;

class RemoteWasStock final : MultiStockClass {
	class MultiClientStockClass final : public StockClass {
	public:
		/* virtual methods from class StockClass */
		void Create(CreateStockItem c, StockRequest request,
			    CancellablePointer &cancel_ptr) override;
	};

	MultiClientStockClass multi_client_stock_class;

	MultiStock multi_stock;

public:
	RemoteWasStock(unsigned limit, unsigned max_idle,
		       EventLoop &event_loop) noexcept;

	void FadeAll() noexcept {
		multi_stock.FadeAll();
	}

	/**
	 * The resulting #StockItem will be a #WasStockConnection
	 * instance.
	 */
	void Get(AllocatorPtr alloc, SocketAddress address,
		 unsigned parallelism, unsigned concurrency,
		 StockGetHandler &handler,
		 CancellablePointer &cancel_ptr) noexcept;

private:
	/* virtual methods from class MultiStockClass */
	std::size_t GetLimit(const void *request,
			     std::size_t _limit) const noexcept override;

	Event::Duration GetClearInterval(const void *) const noexcept override {
		return std::chrono::minutes{5};
	}

	StockItem *Create(CreateStockItem c, StockItem &shared_item) override;
};
