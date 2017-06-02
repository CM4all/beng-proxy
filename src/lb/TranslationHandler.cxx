/*
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "TranslationHandler.hxx"
#include "TranslationCache.hxx"
#include "GotoConfig.hxx"
#include "GotoMap.hxx"
#include "translation/Stock.hxx"
#include "translation/Request.hxx"
#include "translation/Response.hxx"
#include "translation/Handler.hxx"
#include "http_server/Request.hxx"
#include "pool.hxx"

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
     stock(tstock_new(event_loop, config.address, 0)),
     destinations(ToInstance(goto_map, config))
{
}

LbTranslationHandler::~LbTranslationHandler()
{
    tstock_free(stock);
}

void
LbTranslationHandler::FlushCache()
{
    cache.reset();
}

static void
Fill(TranslateRequest &t, const char *name,
     const HttpServerRequest &request)
{
    t.Clear();
    t.pool = name;
    t.host = request.headers.Get("host");
}

struct LbTranslateHandlerRequest {
    LbTranslationHandler &th;

    const HttpServerRequest &http_request;

    TranslateRequest request;

    const TranslateHandler &handler;
    void *handler_ctx;

    LbTranslateHandlerRequest(LbTranslationHandler &_th,
                              const char *name,
                              const HttpServerRequest &_request,
                              const TranslateHandler &_handler, void *_ctx)
        :th(_th), http_request(_request), handler(_handler), handler_ctx(_ctx)
    {
        Fill(request, name, _request);
    }
};

static void
lbth_translate_response(TranslateResponse &response, void *ctx)
{
    auto &r = *(LbTranslateHandlerRequest *)ctx;

    r.th.PutCache(r.http_request, response);
    r.handler.response(response, r.handler_ctx);
}

static void
lbth_translate_error(std::exception_ptr ep, void *ctx)
{
    auto &r = *(LbTranslateHandlerRequest *)ctx;

    r.handler.error(ep, r.handler_ctx);
}

static constexpr TranslateHandler lbth_translate_handler = {
    .response = lbth_translate_response,
    .error = lbth_translate_error,
};

void
LbTranslationHandler::Pick(struct pool &pool, const HttpServerRequest &request,
                           const TranslateHandler &handler, void *ctx,
                           CancellablePointer &cancel_ptr)
{
    if (cache) {
        const auto *item = cache->Get(request);
        if (item != nullptr) {
            /* cache hit */

            TranslateResponse response;
            response.Clear();
            response.status = item->status;
            response.redirect = item->redirect.empty() ? nullptr : item->redirect.c_str();
            response.message = item->message.empty() ? nullptr : item->message.c_str();
            response.pool = item->pool.empty() ? nullptr : item->pool.c_str();
            response.canonical_host = item->canonical_host.empty() ? nullptr : item->canonical_host.c_str();

            handler.response(response, ctx);
            return;
        }
    }

    auto *r = NewFromPool<LbTranslateHandlerRequest>(pool,
                                                      *this, name, request,
                                                      handler, ctx);
    tstock_translate(*stock, pool, r->request,
                     lbth_translate_handler, r, cancel_ptr);
}

void
LbTranslationHandler::PutCache(const HttpServerRequest &request,
                               const TranslateResponse &response)
{
    if (!cache)
        cache.reset(new LbTranslationCache());

    cache->Put(request, response);
}
