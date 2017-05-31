/*
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_LB_TRANSLATION_HANDLER_MAP_HXX
#define BENG_LB_TRANSLATION_HANDLER_MAP_HXX

#include "TranslationHandler.hxx"
#include "util/StringLess.hxx"

#include <map>

struct LbConfig;
class EventLoop;

class LbTranslationHandlerMap {
    std::map<const char *, LbTranslationHandler, StringLess> handlers;

public:
    void Clear() {
        handlers.clear();
    }

    void Scan(const LbConfig &config, EventLoop &event_loop);

    LbTranslationHandler *Find(const char *name) {
        auto i = handlers.find(name);
        return i != handlers.end()
            ? &i->second
            : nullptr;
    }

private:
    void Scan(const LbTranslationHandlerConfig &config, EventLoop &event_loop);
};

#endif
