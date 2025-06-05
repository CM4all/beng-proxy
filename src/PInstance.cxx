// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <max.kellermann@ionos.com>

#include "PInstance.hxx"
#include "pool/pool.hxx"

PInstance::PInstance()
{
#ifndef NDEBUG
	event_loop.SetPostCallback(BIND_FUNCTION(pool_commit));
#endif
}

PInstance::~PInstance()
{
}
