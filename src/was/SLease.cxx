// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#include "SLease.hxx"
#include "SConnection.hxx"

void
WasStockLease::ReleaseWas(PutAction action) noexcept
{
	connection.Put(action);
	Destroy();
}

void
WasStockLease::ReleaseWasStop(uint64_t input_received) noexcept
{
	connection.Stop(input_received);
	connection.Put(PutAction::REUSE);
	Destroy();
}
