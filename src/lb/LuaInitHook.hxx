/*
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_LB_LUA_INIT_HOOK_HXX
#define BENG_LB_LUA_INIT_HOOK_HXX

#include "lua/InitHook.hxx"

struct LbConfig;
class LbClusterMap;
class MyAvahiClient;

class LbLuaInitHook final : public LuaInitHook {
    const LbConfig &config;

    LbClusterMap *const clusters;
    MyAvahiClient *const avahi_client;

public:
    LbLuaInitHook(const LbConfig &_config,
                  LbClusterMap *_clusters, MyAvahiClient *_avahi_client)
        :config(_config), clusters(_clusters), avahi_client(_avahi_client) {}

    int GetPool(lua_State *L, const char *name);

    /* virtual methods from class LuaInitHook */
    void PreInit(lua_State *L) override;
    void PostInit(lua_State *L) override;
};

#endif
