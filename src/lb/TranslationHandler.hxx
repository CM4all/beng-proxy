/*
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_LB_TRANSLATION_HANDLER_HXX
#define BENG_LB_TRANSLATION_HANDLER_HXX

#include "util/StringLess.hxx"

#include <map>

struct LbTranslationHandlerConfig;
class LbCluster;
class LbGotoMap;
struct HttpServerRequest;
struct TranslateHandler;
class TranslateStock;
class EventLoop;
class CancellablePointer;

class LbTranslationHandler final {
    const char *const name;

    TranslateStock *const stock;

    const std::map<const char *, LbCluster &, StringLess> clusters;

public:
    LbTranslationHandler(EventLoop &event_loop, LbGotoMap &goto_map,
                         const LbTranslationHandlerConfig &_config);
    ~LbTranslationHandler();

    LbCluster *FindCluster(const char *cluster_name) const {
        auto i = clusters.find(cluster_name);
        return i != clusters.end()
            ? &i->second
            : nullptr;
    }

    void Pick(struct pool &pool, const HttpServerRequest &request,
              const TranslateHandler &handler, void *ctx,
              CancellablePointer &cancel_ptr);
};

#endif
