/*
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_LB_GOTO_MAP_HXX
#define BENG_LB_GOTO_MAP_HXX

#include "ClusterMap.hxx"
#include "LuaHandler.hxx"
#include "TranslationHandlerMap.hxx"

class LbGotoMap final {
    /**
     * A map of clusters which need run-time data.
     */
    LbClusterMap clusters;

    LbTranslationHandlerMap translation_handlers;

    /**
     * A map of configured #LbLuaHandler instances.
     */
    LbLuaHandlerMap lua_handlers;

public:
    void Clear() {
        translation_handlers.Clear();
    }

    void Scan(const LbConfig &config, MyAvahiClient &avahi_client);

    LbCluster *FindCluster(const char *name) {
        return clusters.Find(name);
    }

    template<typename F>
    void ForEachCluster(F &&f) {
        clusters.ForEach(std::forward<F>(f));
    }

    LbTranslationHandler *FindTranslationHandler(const char *name) {
        return translation_handlers.Find(name);
    }

    LbLuaHandler *FindLuaHandler(const char *name) {
        return lua_handlers.Find(name);
    }
};

#endif
