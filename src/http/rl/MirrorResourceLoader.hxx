// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#include "ResourceLoader.hxx"

class MirrorResourceLoader final : public ResourceLoader {
public:
	/* virtual methods from class ResourceLoader */
	void SendRequest(struct pool &pool,
			 const StopwatchPtr &parent_stopwatch,
			 const ResourceRequestParams &params,
			 HttpMethod method,
			 const ResourceAddress &address,
			 StringMap &&headers,
			 UnusedIstreamPtr body,
			 HttpResponseHandler &handler,
			 CancellablePointer &cancel_ptr) noexcept override;
};
