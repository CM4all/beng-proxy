/*
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "TranslationHandler.hxx"
#include "GotoConfig.hxx"
#include "GotoMap.hxx"
#include "translation/Stock.hxx"
#include "translation/Request.hxx"
#include "http_server/Request.hxx"
#include "pool.hxx"

static std::map<const char *, LbCluster &, StringLess>
ToInstance(LbGotoMap &goto_map, const LbTranslationHandlerConfig &config)
{
    std::map<const char *, LbCluster &, StringLess> map;

    for (const auto &i : config.clusters)
        map.emplace(i.first, goto_map.GetInstance(i.second));

    return map;
}

LbTranslationHandler::LbTranslationHandler(EventLoop &event_loop,
                                           LbGotoMap &goto_map,
                                           const LbTranslationHandlerConfig &config)
    :name(config.name.c_str()),
     stock(tstock_new(event_loop, config.address, 0)),
     clusters(ToInstance(goto_map, config))
{
}

LbTranslationHandler::~LbTranslationHandler()
{
    tstock_free(stock);
}

static void
Fill(TranslateRequest &t, const char *name,
     const HttpServerRequest &request)
{
    t.Clear();
    t.pool = name;
    t.host = request.headers.Get("host");
}

void
LbTranslationHandler::Pick(struct pool &pool, const HttpServerRequest &request,
                           const TranslateHandler &handler, void *ctx,
                           CancellablePointer &cancel_ptr)
{
    auto *tr = NewFromPool<TranslateRequest>(pool);
    Fill(*tr, name, request);

    // TODO: WANT?
    // TODO: cache

    tstock_translate(*stock, pool, *tr, handler, ctx, cancel_ptr);
}
