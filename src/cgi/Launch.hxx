// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <max.kellermann@ionos.com>

#pragma once

#include <cstdint>

enum class HttpMethod : uint_least8_t;
struct pool;
class EventLoop;
class UnusedIstreamPtr;
class SpawnService;
struct CgiAddress;
class StringMap;

/**
 * Launch a CGI script.
 *
 * Throws std::runtime_error on error.
 */
UnusedIstreamPtr
cgi_launch(EventLoop &event_loop, struct pool *pool, HttpMethod method,
	   const CgiAddress *address,
	   const char *remote_addr,
	   const StringMap &headers, UnusedIstreamPtr body,
	   SpawnService &spawn_service);
