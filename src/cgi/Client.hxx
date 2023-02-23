// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#pragma once

struct pool;
class StopwatchPtr;
class UnusedIstreamPtr;
class HttpResponseHandler;
class CancellablePointer;

/**
 * Communicate with a CGI script.
 *
 * @param input the stream received from the child process
 */
void
cgi_client_new(struct pool &pool, StopwatchPtr stopwatch,
	       UnusedIstreamPtr input,
	       HttpResponseHandler &handler,
	       CancellablePointer &cancel_ptr);
