// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <max.kellermann@ionos.com>

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
		       StringMap &&headers,
		       UnusedIstreamPtr &&body) noexcept
		:CoHttpResponseHandler(_pool)
	{
		rl.SendRequest(_pool, parent_stopwatch, params,
			       method, address, std::move(headers),
			       std::move(body),
			       *this, cancel_ptr);
	}
};
