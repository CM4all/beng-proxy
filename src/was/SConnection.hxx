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

#include "IdleConnection.hxx"
#include "stock/Item.hxx"
#include "io/Logger.hxx"

class WasStockConnection
	: public StockItem, WasIdleConnectionHandler
{
	LLogger logger;

	WasIdleConnection connection;

public:
	explicit WasStockConnection(CreateStockItem c) noexcept;

	auto &GetEventLoop() const noexcept {
		return connection.GetEventLoop();
	}

	const auto &GetSocket() const noexcept {
		return connection.GetSocket();
	}

	/**
	 * Set the "stopping" flag.  Call this after sending
	 * #WAS_COMMAND_STOP, before calling hstock_put().  This will
	 * make the stock wait for #WAS_COMMAND_PREMATURE.
	 */
	void Stop(uint64_t _received) noexcept;

	virtual void SetSite([[maybe_unused]] const char *site) noexcept {}
	virtual void SetUri([[maybe_unused]] const char *uri) noexcept {}

protected:
	void Open(WasSocket &&_socket) noexcept {
		connection.Open(std::move(_socket));
	}

private:
	/* virtual methods from class StockItem */
	bool Borrow() noexcept override;
	bool Release() noexcept override;

	/* virtual methods from class WasIdleConnectionHandler */
	void OnWasIdleConnectionClean() noexcept override;
	void OnWasIdleConnectionError(std::exception_ptr e) noexcept override;
};
