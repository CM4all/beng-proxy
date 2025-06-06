// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <max.kellermann@ionos.com>

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
	WasStockConnection(CreateStockItem c, WasSocket &&_socket) noexcept;

#ifdef HAVE_URING
	void EnableUring(Uring::Queue &uring_queue) {
		connection.EnableUring(uring_queue);
	}
#endif

	auto &GetEventLoop() const noexcept {
		return connection.GetEventLoop();
	}

	auto &GetControl() noexcept {
		return connection.GetControl();
	}

	FileDescriptor GetInput() const noexcept {
		return connection.GetInput();
	}

	FileDescriptor GetOutput() const noexcept {
		return connection.GetOutput();
	}

	/**
	 * Set the "stopping" flag.  Call this after sending
	 * #WAS_COMMAND_STOP, before calling hstock_put().  This will
	 * make the stock wait for #WAS_COMMAND_PREMATURE.
	 */
	void Stop(uint_least64_t _received) noexcept;

	virtual void SetSite([[maybe_unused]] const char *site) noexcept {}
	virtual void SetUri([[maybe_unused]] const char *uri) noexcept {}

protected:
	/* virtual methods from class StockItem */
	bool Borrow() noexcept override;
	bool Release() noexcept override;

private:
	/* virtual methods from class WasIdleConnectionHandler */
	void OnWasIdleConnectionClean() noexcept override;
	void OnWasIdleConnectionError(std::exception_ptr e) noexcept override;
};
