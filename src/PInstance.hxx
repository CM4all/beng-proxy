// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <max.kellermann@ionos.com>

#pragma once

#include "pool/RootPool.hxx"
#include "event/Loop.hxx"

/**
 * A base class for "Instance" structs which manages an #EventLoop and
 * a #RootPool.
 */
struct PInstance {
	EventLoop event_loop;

	RootPool root_pool;

	PInstance();
	~PInstance();
};
