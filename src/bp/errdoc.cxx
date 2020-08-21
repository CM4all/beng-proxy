/*
 * Copyright 2007-2020 CM4all GmbH
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

#include "errdoc.hxx"
#include "Request.hxx"
#include "PendingResponse.hxx"
#include "Instance.hxx"
#include "http/IncomingRequest.hxx"
#include "http/Headers.hxx"
#include "HttpResponseHandler.hxx"
#include "translation/Service.hxx"
#include "translation/Handler.hxx"
#include "ResourceLoader.hxx"
#include "istream/istream.hxx"
#include "istream/UnusedHoldPtr.hxx"
#include "pool/pool.hxx"
#include "io/Logger.hxx"

struct ErrorResponseLoader final : TranslateHandler, HttpResponseHandler, Cancellable {
	CancellablePointer cancel_ptr;

	Request &request;

	PendingResponse original_response;

	TranslateRequest translate_request;

	ErrorResponseLoader(Request &_request, http_status_t _status,
			    HttpHeaders &&_headers, UnusedIstreamPtr _body)
		:request(_request),
		 original_response(_status, std::move(_headers),
				   UnusedHoldIstreamPtr{request.pool, std::move(_body)}) {}

	void Destroy() {
		this->~ErrorResponseLoader();
	}

	void ResubmitAndDestroy() noexcept {
		auto &_request = request;
		auto _response = std::move(original_response);

		Destroy();

		_request.DispatchResponse(_response.status,
					  std::move(_response.headers),
					  std::move(_response.body));
	}

	/* virtual methods from class Cancellable */
	void Cancel() noexcept override;

	/* virtual methods from TranslateHandler */
	void OnTranslateResponse(TranslateResponse &response) noexcept override;
	void OnTranslateError(std::exception_ptr error) noexcept override;

	/* virtual methods from class HttpResponseHandler */
	void OnHttpResponse(http_status_t status, StringMap &&headers,
			    UnusedIstreamPtr body) noexcept override;
	void OnHttpError(std::exception_ptr ep) noexcept override;
};

/*
 * HTTP response handler
 *
 */

void
ErrorResponseLoader::OnHttpResponse(http_status_t _status, StringMap &&_headers,
				    UnusedIstreamPtr _body) noexcept
{
	if (http_status_is_success(_status)) {
		auto &_request = request;
		const auto status = original_response.status;
		Destroy();
		_request.InvokeResponse(status, std::move(_headers), std::move(_body));
	} else {
		/* close the new response body */
		_body.Clear();

		ResubmitAndDestroy();
	}
}

void
ErrorResponseLoader::OnHttpError(std::exception_ptr ep) noexcept
{
	LogConcat(2, request.request.uri, "error on error document: ", ep);

	ResubmitAndDestroy();
}

/*
 * translate handler
 *
 */

void
ErrorResponseLoader::OnTranslateResponse(TranslateResponse &response) noexcept
{
	if ((response.status == (http_status_t)0 ||
	     http_status_is_success(response.status)) &&
	    response.address.IsDefined()) {

		request.instance.cached_resource_loader
			->SendRequest(request.pool, request.stopwatch,
				      0, nullptr, nullptr,
				      HTTP_METHOD_GET,
				      response.address, HTTP_STATUS_OK,
				      {}, nullptr, nullptr,
				      *this, request.cancel_ptr);
	} else {
		ResubmitAndDestroy();
	}
}

void
ErrorResponseLoader::OnTranslateError(std::exception_ptr ep) noexcept
{
	LogConcat(2, request.request.uri,
		  "error document translation error: ", ep);

	ResubmitAndDestroy();
}

static void
fill_translate_request(TranslateRequest *t,
		       const TranslateRequest *src,
		       ConstBuffer<void> error_document,
		       http_status_t status)
{
	*t = *src;
	t->error_document = error_document;
	t->status = status;
}

/*
 * async operation
 *
 */

void
ErrorResponseLoader::Cancel() noexcept
{
	cancel_ptr.Cancel();
	Destroy();
}

/*
 * constructor
 *
 */

void
errdoc_dispatch_response(Request &request2, http_status_t status,
			 ConstBuffer<void> error_document,
			 HttpHeaders &&headers, UnusedIstreamPtr body)
{
	assert(!error_document.IsNull());

	auto &instance = request2.instance;

	assert(instance.translation_service != nullptr);

	auto *er = NewFromPool<ErrorResponseLoader>(request2.pool, request2,
						    status, std::move(headers),
						    std::move(body));

	request2.cancel_ptr = *er;

	fill_translate_request(&er->translate_request,
			       &request2.translate.request,
			       error_document, status);
	instance.translation_service->SendRequest(request2.pool,
						  er->translate_request,
						  request2.stopwatch,
						  *er, er->cancel_ptr);
}
