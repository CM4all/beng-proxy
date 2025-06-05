// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <max.kellermann@ionos.com>

#include "BlockingResourceLoader.hxx"
#include "istream/UnusedHoldPtr.hxx"
#include "pool/pool.hxx"
#include "pool/Holder.hxx"
#include "util/LeakDetector.hxx"
#include "util/Cancellable.hxx"

class BlockingResourceRequest final : PoolHolder, LeakDetector, Cancellable {
	UnusedHoldIstreamPtr request_body;

public:
	BlockingResourceRequest(struct pool &_pool,
				UnusedIstreamPtr &&_request_body,
				CancellablePointer &cancel_ptr) noexcept
		:PoolHolder(_pool),
		 request_body(GetPool(), std::move(_request_body)) {
		cancel_ptr = *this;
	}

private:
	/* virtual methods from class Cancellable */
	void Cancel() noexcept override {
		this->~BlockingResourceRequest();
	}
};

void
BlockingResourceLoader::SendRequest(struct pool &pool,
				    const StopwatchPtr &,
				    const ResourceRequestParams &,
				    HttpMethod,
				    const ResourceAddress &,
				    StringMap &&,
				    UnusedIstreamPtr body,
				    HttpResponseHandler &,
				    CancellablePointer &cancel_ptr) noexcept
{
	NewFromPool<BlockingResourceRequest>(pool, pool,
					     std::move(body), cancel_ptr);
}
