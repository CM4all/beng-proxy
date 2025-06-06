// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <max.kellermann@ionos.com>

#include "FailingResourceLoader.hxx"
#include "istream/UnusedPtr.hxx"
#include "http/ResponseHandler.hxx"

#include <stdexcept>

void
FailingResourceLoader::SendRequest(struct pool &,
				   const StopwatchPtr &,
				   const ResourceRequestParams &,
				   HttpMethod,
				   const ResourceAddress &,
				   StringMap &&,
				   UnusedIstreamPtr body,
				   HttpResponseHandler &handler,
				   CancellablePointer &) noexcept
{
	body.Clear();

	handler.InvokeError(std::make_exception_ptr(std::runtime_error("unimplemented")));
}
