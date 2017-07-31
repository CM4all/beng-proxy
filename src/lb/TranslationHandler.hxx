/*
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_LB_TRANSLATION_HANDLER_HXX
#define BENG_LB_TRANSLATION_HANDLER_HXX

#include "Goto.hxx"
#include "util/StringLess.hxx"

#include <map>
#include <memory>

struct LbTranslationHandlerConfig;
class LbGotoMap;
struct HttpServerRequest;
struct TranslateRequest;
struct TranslateHandler;
struct TranslateResponse;
class TranslateStock;
class EventLoop;
class CancellablePointer;
class LbTranslationCache;

class LbTranslationHandler final {
    const char *const name;

    TranslateStock *const stock;

    const std::map<const char *, LbGoto, StringLess> destinations;

    std::unique_ptr<LbTranslationCache> cache;

public:
    LbTranslationHandler(EventLoop &event_loop, LbGotoMap &goto_map,
                         const LbTranslationHandlerConfig &_config);
    ~LbTranslationHandler();

    void FlushCache();
    void InvalidateCache(const TranslateRequest &request);

    const LbGoto *FindDestination(const char *destination_name) const {
        auto i = destinations.find(destination_name);
        return i != destinations.end()
            ? &i->second
            : nullptr;
    }

    void Pick(struct pool &pool, const HttpServerRequest &request,
              const char *listener_tag,
              const TranslateHandler &handler, void *ctx,
              CancellablePointer &cancel_ptr);

    void PutCache(const HttpServerRequest &request,
                  const char *listener_tag,
                  const TranslateResponse &response);
};

#endif
