// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#pragma once

#include <cstdint>
#include <span>

enum class HttpStatus : uint_least16_t;
struct pool;
class UnusedIstreamPtr;
class EventLoop;
class SpawnService;
class StringMap;
class HttpResponseHandler;
class StopwatchPtr;
struct ChildOptions;

/**
 * Filter an istream through a piped program.
 *
 * @param status the HTTP status code to be sent to the response
 * handler
 */
void
pipe_filter(SpawnService &spawn_service, EventLoop &event_loop,
	    struct pool &pool,
	    const StopwatchPtr &parent_stopwatch,
	    const char *path,
	    std::span<const char *const> args,
	    const ChildOptions &options,
	    HttpStatus status, StringMap &&headers, UnusedIstreamPtr body,
	    HttpResponseHandler &_handler);
