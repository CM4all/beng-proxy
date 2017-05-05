/*
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "TranslationHandler.hxx"
#include "GotoConfig.hxx"
#include "translation/Stock.hxx"
#include "translation/Request.hxx"
#include "http_server/Request.hxx"
#include "pool.hxx"

LbTranslationHandler::LbTranslationHandler(EventLoop &event_loop,
                                           const LbTranslationHandlerConfig &config)
    :name(config.name.c_str()), stock(tstock_new(event_loop, config.address, 0))
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
