// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <max.kellermann@ionos.com>

/*
 * Node selection by jvmRoute.
 */

#pragma once

#include "cluster/StickyHash.hxx"

class StringMap;
struct LbClusterConfig;

/**
 * Extract a jvm_route cookie from the request headers.
 */
[[gnu::pure]]
sticky_hash_t
lb_jvm_route_get(const StringMap &request_headers,
		 const LbClusterConfig &cluster) noexcept;
