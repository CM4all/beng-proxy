// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <max.kellermann@ionos.com>

#include "Glue.hxx"
#include "Address.hxx"
#include "Client.hxx"
#include "Launch.hxx"
#include "util/AbortFlag.hxx"
#include "stopwatch.hxx"
#include "http/ResponseHandler.hxx"
#include "istream/UnusedPtr.hxx"

void
cgi_new(SpawnService &spawn_service, EventLoop &event_loop,
	struct pool *pool,
	const StopwatchPtr &parent_stopwatch,
	HttpMethod method,
	const CgiAddress *address,
	const char *remote_addr,
	const StringMap &headers, UnusedIstreamPtr body,
	HttpResponseHandler &handler,
	CancellablePointer &cancel_ptr)
{
	StopwatchPtr stopwatch(parent_stopwatch, address->path);

	AbortFlag abort_flag(cancel_ptr);

	UnusedIstreamPtr input;

	try {
		input = cgi_launch(event_loop, pool, method, address,
				   remote_addr, headers, std::move(body),
				   spawn_service);
	} catch (...) {
		if (abort_flag.aborted) {
			/* the operation was aborted - don't call the
			   http_response_handler */
			return;
		}

		handler.InvokeError(std::current_exception());
		return;
	}

	stopwatch.RecordEvent("fork");

	cgi_client_new(*pool, std::move(stopwatch),
		       std::move(input), handler, cancel_ptr);
}
