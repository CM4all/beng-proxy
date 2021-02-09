/*
 * Copyright 2007-2021 CM4all GmbH
 * All rights reserved.
 *
 * author: Max Kellermann <mk@cm4all.com>
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * - Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 *
 * - Redistributions in binary form must reproduce the above copyright
 * notice, this list of conditions and the following disclaimer in the
 * documentation and/or other materials provided with the
 * distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE
 * FOUNDATION OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED
 * OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "Request.hxx"
#include "CoLoadResource.hxx"
#include "PendingResponse.hxx"
#include "Instance.hxx"
#include "http/IncomingRequest.hxx"
#include "translation/CoTranslate.hxx"
#include "io/Logger.hxx"

static auto
MakeErrdocTranslateRequest(TranslateRequest r,
			   ConstBuffer<void> error_document,
			   http_status_t status) noexcept
{
	r.error_document = error_document;
	r.status = status;
	return r;
}

Co::InvokeTask
Request::DispatchErrdocResponse(ConstBuffer<void> error_document)
{
	assert(!error_document.IsNull());
	assert(co_response);

	const auto t = co_await
		CoTranslate(GetTranslationService(), pool,
			    MakeErrdocTranslateRequest(translate.request,
						       error_document,
						       co_response->status),
			    stopwatch);

	if ((t.status != (http_status_t)0 &&
	     !http_status_is_success(t.status)) ||
	    !t.address.IsDefined())
		/* translation server did not specify an error
		   document: submit the original response as-is */
		co_return;

	const auto response = co_await
		CoLoadResource(*instance.cached_resource_loader, pool,
			       stopwatch,
			       0, nullptr, nullptr,
			       HTTP_METHOD_GET,
			       t.address, HTTP_STATUS_OK,
			       {}, nullptr, nullptr);

	if (!http_status_is_success(response->status))
		/* the error document failed: submit the original
		   as-is */
		co_return;

	/* submit the error document which we just received */
	co_response =
		UniquePoolPtr<PendingResponse>::Make(pool, response->status,
						     std::move(response->headers),
						     std::move(response->body));
}

void
Request::OnErrdocCompletion(std::exception_ptr e) noexcept
{
	assert(co_response);

	if (e)
		logger(2, "error on error document: ", e);

	DispatchResponse(co_response->status, std::move(co_response->headers),
			 std::move(co_response->body));
}
