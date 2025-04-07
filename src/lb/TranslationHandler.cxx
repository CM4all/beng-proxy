// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#include "TranslationHandler.hxx"
#include "TranslationCache.hxx"
#include "GotoConfig.hxx"
#include "GotoMap.hxx"
#include "translation/Request.hxx"
#include "translation/Response.hxx"
#include "translation/Handler.hxx"
#include "http/CommonHeaders.hxx"
#include "http/IncomingRequest.hxx"
#include "pool/pool.hxx"
#include "stats/CacheStats.hxx"
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
	 stock(event_loop, config.address, 16),
	 destinations(ToInstance(goto_map, config))
{
}

LbTranslationHandler::~LbTranslationHandler() noexcept = default;

CacheStats
LbTranslationHandler::GetCacheStats() const noexcept
{
	return cache ? cache->GetStats() : CacheStats{};
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
	t.host = request.headers.Get(host_header);
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
			response.arch = item->arch;
			response.site = item->site.empty() ? nullptr : item->site.c_str();
			response.redirect = item->redirect.empty() ? nullptr : item->redirect.c_str();
			response.message = item->message.empty() ? nullptr : item->message.c_str();
			response.pool = item->pool.empty() ? nullptr : item->pool.c_str();
			response.canonical_host = item->canonical_host.empty() ? nullptr : item->canonical_host.c_str();
			response.analytics_id = item->analytics_id.empty() ? nullptr : item->analytics_id.c_str();
			response.generator = item->generator.empty() ? nullptr : item->generator.c_str();

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
		// TODO configurable cache size
		cache.reset(new LbTranslationCache(256 * std::size_t{1024 * 1024}));

	cache->Put(request, listener_tag, response);
}
