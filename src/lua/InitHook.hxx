/*
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef LUA_INIT_HOOK_HXX
#define LUA_INIT_HOOK_HXX

struct lua_State;

class LuaInitHook {
public:
    virtual void PreInit(lua_State *L) = 0;
    virtual void PostInit(lua_State *L) = 0;
};

class NopLuaInitHook : public LuaInitHook {
public:
    void PreInit(lua_State *) override {}
    void PostInit(lua_State *) override {}
};

#endif
