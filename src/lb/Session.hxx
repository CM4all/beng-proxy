// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <max.kellermann@ionos.com>

/*
 * Session handling.
 */

#pragma once

#include "cluster/StickyHash.hxx"

class StringMap;

/**
 * Extract a session identifier from the request headers.
 */
sticky_hash_t
lb_session_get(const StringMap &request_headers,
	       const char *cookie_name);
