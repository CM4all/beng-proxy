/*
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_LB_TRANSLATION_HANDLER_HXX
#define BENG_LB_TRANSLATION_HANDLER_HXX

struct LbTranslationHandlerConfig;
struct HttpServerRequest;
struct TranslateHandler;
class TranslateStock;
class EventLoop;
class CancellablePointer;

class LbTranslationHandler final {
    const char *const name;

    TranslateStock *const stock;

public:
    LbTranslationHandler(EventLoop &event_loop,
                         const LbTranslationHandlerConfig &_config);
    ~LbTranslationHandler();

    void Pick(struct pool &pool, const HttpServerRequest &request,
              const TranslateHandler &handler, void *ctx,
              CancellablePointer &cancel_ptr);
};

#endif
