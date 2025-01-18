// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#include "SLease.hxx"
#include "SConnection.hxx"

PutAction
WasStockLease::ReleaseWas(PutAction action) noexcept
{
	action = connection.Put(action);
	Destroy();
	return action;
}

PutAction
WasStockLease::ReleaseWasStop(uint64_t input_received) noexcept
{
	connection.Stop(input_received);
	PutAction action = connection.Put(PutAction::REUSE);
	Destroy();
	return action;
}
