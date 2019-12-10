/*
 * Copyright 2007-2019 CM4all GmbH
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

#include "HttpConnection.hxx"
#include "GotoConfig.hxx"
#include "TranslationHandler.hxx"
#include "Config.hxx"
#include "http_server/http_server.hxx"
#include "http/IncomingRequest.hxx"
#include "translation/Handler.hxx"
#include "translation/Response.hxx"
#include "pool/pool.hxx"
#include "RedirectHttps.hxx"
#include "istream/UnusedHoldPtr.hxx"
#include "util/Cancellable.hxx"
#include "util/LeakDetector.hxx"
#include "AllocatorPtr.hxx"

/*
 * TranslateHandler
 *
 */

struct LbHttpRequest final : private Cancellable, TranslateHandler, private LeakDetector {
	struct pool &pool;
	LbHttpConnection &connection;
	LbTranslationHandler &handler;
	IncomingHttpRequest &request;

	/**
	 * This object temporarily holds the request body
	 */
	UnusedHoldIstreamPtr request_body;

	CancellablePointer &caller_cancel_ptr;
	CancellablePointer translate_cancel_ptr;

	LbHttpRequest(LbHttpConnection &_connection,
		      LbTranslationHandler &_handler,
		      IncomingHttpRequest &_request,
		      CancellablePointer &_cancel_ptr)
		:pool(_request.pool), connection(_connection), handler(_handler),
		 request(_request),
		 request_body(request.pool, std::move(request.body)),
		 caller_cancel_ptr(_cancel_ptr) {
		caller_cancel_ptr = *this;
	}

	void Destroy() noexcept {
		DeleteFromPool(pool, this);
	}

private:
	/* virtual methods from class Cancellable */
	void Cancel() noexcept override {
		CancellablePointer cancel_ptr(std::move(translate_cancel_ptr));
		Destroy();
		cancel_ptr.Cancel();
	}

	/* virtual methods from TranslateHandler */
	void OnTranslateResponse(TranslateResponse &response) noexcept override;
	void OnTranslateError(std::exception_ptr error) noexcept override;
};

void
LbHttpRequest::OnTranslateResponse(TranslateResponse &response) noexcept
{
	auto &_request = request;
	auto &c = connection;

	if (response.site != nullptr)
		c.per_request.site_name = p_strdup(request.pool, response.site);

	if (response.https_only != 0 && !c.IsEncrypted()) {
		Destroy();

		const char *host = c.per_request.host;
		if (host == nullptr) {
			_request.SendMessage(HTTP_STATUS_BAD_REQUEST, "No Host header");
			return;
		}

		_request.SendRedirect(HTTP_STATUS_MOVED_PERMANENTLY,
				      MakeHttpsRedirect(AllocatorPtr{_request.pool},
							host,
							response.https_only,
							_request.uri),
				      "This page requires \"https\"");
	} else if (response.status != http_status_t(0) ||
		   response.redirect != nullptr ||
		   response.message != nullptr) {
		Destroy();

		auto status = response.status;
		if (status == http_status_t(0))
			status = HTTP_STATUS_SEE_OTHER;

		const char *body = response.message;
		if (body == nullptr)
			body = http_status_to_string(status);

		_request.SendSimpleResponse(status, response.redirect, body);
	} else if (response.pool != nullptr) {
		auto *destination = handler.FindDestination(response.pool);
		if (destination == nullptr) {
			Destroy();

			c.LogSendError(_request,
				       std::make_exception_ptr(std::runtime_error("No such pool")));
			return;
		}

		if (response.canonical_host != nullptr)
			c.per_request.canonical_host = response.canonical_host;

		request.body = std::move(request_body);

		auto &_caller_cancel_ptr = caller_cancel_ptr;
		Destroy();

		c.HandleHttpRequest(*destination, _request, _caller_cancel_ptr);
	} else {
		Destroy();

		c.LogSendError(_request,
			       std::make_exception_ptr(std::runtime_error("Invalid translation server response")));
	}
}

void
LbHttpRequest::OnTranslateError(std::exception_ptr ep) noexcept
{
	auto &_request = request;
	auto &_connection = connection;

	Destroy();

	_connection.LogSendError(_request, ep);
}

/*
 * constructor
 *
 */

void
LbHttpConnection::AskTranslationServer(LbTranslationHandler &handler,
				       IncomingHttpRequest &request,
				       CancellablePointer &cancel_ptr)
{
	auto *r = NewFromPool<LbHttpRequest>(request.pool, *this, handler, request,
					     cancel_ptr);

	handler.Pick(request.pool, request,
		     listener.tag.empty() ? nullptr : listener.tag.c_str(),
		     *r, r->translate_cancel_ptr);
}
