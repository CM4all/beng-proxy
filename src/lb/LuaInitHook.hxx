/*
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_LB_LUA_INIT_HOOK_HXX
#define BENG_LB_LUA_INIT_HOOK_HXX

#include "lua/InitHook.hxx"

class LbGotoMap;

class LbLuaInitHook final : public LuaInitHook {
    LbGotoMap *const goto_map;

public:
    explicit LbLuaInitHook(LbGotoMap *_goto_map)
        :goto_map(_goto_map) {}

    int GetPool(lua_State *L, const char *name);

    /* virtual methods from class LuaInitHook */
    void PreInit(lua_State *L) override;
    void PostInit(lua_State *L) override;
};

#endif
