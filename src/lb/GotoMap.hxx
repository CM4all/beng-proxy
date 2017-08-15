/*
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_LB_GOTO_MAP_HXX
#define BENG_LB_GOTO_MAP_HXX

#include "Cluster.hxx"
#include "Branch.hxx"
#include "TranslationHandler.hxx"
#include "LuaHandler.hxx"
#include "LuaInitHook.hxx"

#include <map>

struct LbGoto;
struct LbGotoConfig;
struct TranslationInvalidateRequest;

class LbGotoMap final {
    const LbConfig &root_config;
    MyAvahiClient &avahi_client;

    LbLuaInitHook lua_init_hook;

    std::map<const LbClusterConfig *, LbCluster> clusters;
    std::map<const LbBranchConfig *, LbBranch> branches;
    std::map<const LbTranslationHandlerConfig *,
             LbTranslationHandler> translation_handlers;
    std::map<const LbLuaHandlerConfig *,
             LbLuaHandler> lua_handlers;

public:
    LbGotoMap(const LbConfig &_config,
              MyAvahiClient &_avahi_client)
        :root_config(_config), avahi_client(_avahi_client),
         lua_init_hook(this) {}

    LbGotoMap(const LbGotoMap &) = delete;
    LbGotoMap &operator=(const LbGotoMap &) = delete;

    void Clear() {
        translation_handlers.clear();
    }

    void FlushCaches();
    void InvalidateTranslationCaches(const TranslationInvalidateRequest &request);

    template<typename F>
    void ForEachCluster(F &&f) {
        for (auto &i : clusters)
            f(i.second);
    }

    LbGoto GetInstance(const char *name);
    LbGoto GetInstance(const LbGotoConfig &config);

    LbCluster &GetInstance(const LbClusterConfig &config);

private:
    LbBranch &GetInstance(const LbBranchConfig &config);
    LbLuaHandler &GetInstance(const LbLuaHandlerConfig &config);
    LbTranslationHandler &GetInstance(const LbTranslationHandlerConfig &config);
};

#endif
