// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#pragma once

#include "Lease.hxx"

class WasStockConnection;

class WasStockLease final : public WasLease {
	WasStockConnection &connection;

public:
	explicit WasStockLease(WasStockConnection &_connection) noexcept
		:connection(_connection) {}

	WasStockLease(const WasStockLease &) = delete;
	WasStockLease &operator=(const WasStockLease &) = delete;

	PutAction ReleaseWas(PutAction action) noexcept override;
	PutAction ReleaseWasStop(uint_least64_t input_received) noexcept override;

private:
	void Destroy() noexcept {
		this->~WasStockLease();
	}
};
