/*
 * Copyright 2007-2022 CM4all GmbH
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

#include "TranslationHandler.hxx"
#include "TranslationCache.hxx"
#include "GotoConfig.hxx"
#include "GotoMap.hxx"
#include "translation/Request.hxx"
#include "translation/Response.hxx"
#include "translation/Handler.hxx"
#include "http/IncomingRequest.hxx"
#include "pool/pool.hxx"
#include "stopwatch.hxx"
#include "AllocatorPtr.hxx"

static std::map<const char *, LbGoto, StringLess>
ToInstance(LbGotoMap &goto_map, const LbTranslationHandlerConfig &config)
{
	std::map<const char *, LbGoto, StringLess> map;

	for (const auto &i : config.destinations)
		map.emplace(i.first, goto_map.GetInstance(i.second));

	return map;
}

LbTranslationHandler::LbTranslationHandler(EventLoop &event_loop,
					   LbGotoMap &goto_map,
					   const LbTranslationHandlerConfig &config)
	:name(config.name.c_str()),
	 stock(event_loop, config.address, 0),
	 destinations(ToInstance(goto_map, config))
{
}

LbTranslationHandler::~LbTranslationHandler() noexcept = default;

size_t
LbTranslationHandler::GetAllocatedCacheMemory() const noexcept
{
	return cache ? cache->GetAllocatedMemory() : 0;
}

void
LbTranslationHandler::FlushCache()
{
	cache.reset();
}

void
LbTranslationHandler::InvalidateCache(const TranslationInvalidateRequest &request)
{
	if (cache)
		cache->Invalidate(request);
}

static void
Fill(TranslateRequest &t, const char *name,
     const char *listener_tag,
     const IncomingHttpRequest &request)
{
	t.pool = name;
	t.listener_tag = listener_tag;
	t.host = request.headers.Get("host");
}

struct LbTranslateHandlerRequest final : TranslateHandler {
	LbTranslationHandler &th;

	const IncomingHttpRequest &http_request;
	const char *const listener_tag;

	TranslateRequest request;

	TranslateHandler &handler;

	LbTranslateHandlerRequest(LbTranslationHandler &_th,
				  const char *name,
				  const char *_listener_tag,
				  const IncomingHttpRequest &_request,
				  TranslateHandler &_handler)
		:th(_th), http_request(_request), listener_tag(_listener_tag),
		 handler(_handler)
	{
		Fill(request, name, listener_tag, _request);
	}

	/* virtual methods from TranslateHandler */
	void OnTranslateResponse(UniquePoolPtr<TranslateResponse> response) noexcept override;
	void OnTranslateError(std::exception_ptr error) noexcept override;
};

void
LbTranslateHandlerRequest::OnTranslateResponse(UniquePoolPtr<TranslateResponse> response) noexcept
{
	th.PutCache(http_request, listener_tag, *response);
	handler.OnTranslateResponse(std::move(response));
}

void
LbTranslateHandlerRequest::OnTranslateError(std::exception_ptr ep) noexcept
{
	handler.OnTranslateError(ep);
}

void
LbTranslationHandler::Pick(struct pool &pool, const IncomingHttpRequest &request,
			   const char *listener_tag,
			   TranslateHandler &handler,
			   CancellablePointer &cancel_ptr)
{
	if (cache) {
		const auto *item = cache->Get(request, listener_tag);
		if (item != nullptr) {
			/* cache hit */

			auto _response = UniquePoolPtr<TranslateResponse>::Make(pool);
			auto &response = *_response;
			response.Clear();
			response.status = item->status;
			response.https_only = item->https_only;
			response.site = item->site.empty() ? nullptr : item->site.c_str();
			response.redirect = item->redirect.empty() ? nullptr : item->redirect.c_str();
			response.message = item->message.empty() ? nullptr : item->message.c_str();
			response.pool = item->pool.empty() ? nullptr : item->pool.c_str();
			response.canonical_host = item->canonical_host.empty() ? nullptr : item->canonical_host.c_str();

			handler.OnTranslateResponse(std::move(_response));
			return;
		}
	}

	auto *r = NewFromPool<LbTranslateHandlerRequest>(pool,
							 *this, name, listener_tag,
							 request,
							 handler);
	stock.SendRequest(pool, r->request,
			  nullptr,
			  *r, cancel_ptr);
}

void
LbTranslationHandler::PutCache(const IncomingHttpRequest &request,
			       const char *listener_tag,
			       const TranslateResponse &response)
{
	if (response.max_age == std::chrono::seconds::zero())
		/* not cacheable */
		return;

	if (!cache)
		cache.reset(new LbTranslationCache());

	cache->Put(request, listener_tag, response);
}
