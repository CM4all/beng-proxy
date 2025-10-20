// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <max.kellermann@ionos.com>

#include "Request.hxx"
#include "CoLoadResource.hxx"
#include "PendingResponse.hxx"
#include "Instance.hxx"
#include "http/IncomingRequest.hxx"
#include "http/Method.hxx"
#include "translation/CoTranslate.hxx"
#include "co/Task.hxx"
#include "io/Logger.hxx"

static auto
MakeErrdocTranslateRequest(TranslateRequest r,
			   std::span<const std::byte> error_document,
			   HttpStatus status) noexcept
{
	r.error_document = error_document;
	r.status = status;
	return r;
}

Co::Task<PendingResponse>
Request::DispatchErrdocResponse(std::span<const std::byte> error_document)
{
	assert(error_document.data() != nullptr);
	assert(co_response);

	const auto tp = co_await
		CoTranslate(GetTranslationService(), pool,
			    MakeErrdocTranslateRequest(translate.request,
						       error_document,
						       co_response->status),
			    stopwatch);
	const auto &t = *tp;

	if ((t.status != HttpStatus{} &&
	     !http_status_is_success(t.status)) ||
	    !t.address.IsDefined())
		/* translation server did not specify an error
		   document: submit the original response as-is */
		co_return std::move(*co_response);

	const auto response = co_await
		CoLoadResource(*instance.cached_resource_loader, pool,
			       stopwatch, {},
			       HttpMethod::GET,
			       t.address,
			       {}, nullptr);

	if (!http_status_is_success(response->status))
		/* the error document failed: submit the original
		   as-is */
		co_return std::move(*co_response);

	/* submit the error document which we just received */
	co_return PendingResponse(response->status,
				  std::move(response->headers),
				  std::move(response->body));
}

void
Request::OnErrdocCompletion(std::exception_ptr &&e) noexcept
{
	assert(co_response);

	if (e)
		logger(2, "error on error document: ", std::move(e));

	DispatchResponse(std::move(*co_response));
}
