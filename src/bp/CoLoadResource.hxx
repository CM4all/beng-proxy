// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#pragma once

#include "http/CoResponseHandler.hxx"
#include "http/rl/ResourceLoader.hxx"

class CoLoadResource final : public CoHttpResponseHandler {
public:
	CoLoadResource(ResourceLoader &rl, struct pool &_pool,
		       const StopwatchPtr &parent_stopwatch,
		       const ResourceRequestParams &params,
		       HttpMethod method,
		       const ResourceAddress &address,
		       HttpStatus status, StringMap &&headers,
		       UnusedIstreamPtr &&body, const char *body_etag) noexcept
		:CoHttpResponseHandler(_pool)
	{
		rl.SendRequest(_pool, parent_stopwatch, params,
			       method, address, status, std::move(headers),
			       std::move(body), body_etag,
			       *this, cancel_ptr);
	}
};
