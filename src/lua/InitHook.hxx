// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <max.kellermann@ionos.com>

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
