// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#pragma once

#include "ResourceLoader.hxx"

class EventLoop;
class PipeStock;

/**
 * A #ResourceLoader implementation which uses #BufferedIstream to
 * postpone the real #ResourceLoader call.
 */
class BufferedResourceLoader final : public ResourceLoader {
	EventLoop &event_loop;
	ResourceLoader &next;

	PipeStock *const pipe_stock;

	class Request;

public:
	BufferedResourceLoader(EventLoop &_event_loop,
			       ResourceLoader &_next,
			       PipeStock *_pipe_stock) noexcept
		:event_loop(_event_loop), next(_next), pipe_stock(_pipe_stock) {}

	/* virtual methods from class ResourceLoader */
	void SendRequest(struct pool &pool,
			 const StopwatchPtr &parent_stopwatch,
			 const ResourceRequestParams &params,
			 HttpMethod method,
			 const ResourceAddress &address,
			 HttpStatus status, StringMap &&headers,
			 UnusedIstreamPtr body, const char *body_etag,
			 HttpResponseHandler &handler,
			 CancellablePointer &cancel_ptr) noexcept override;
};
