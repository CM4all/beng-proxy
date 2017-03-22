/*
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "lb_instance.hxx"
#include "lb_lua.hxx"
#include "lua/RunFile.hxx"

extern "C" {
#include <lauxlib.h>
#include <lualib.h>
}

void
LbInstance::EnableLua()
{
    assert(!lua_state);

    lua_State *L = luaL_newstate();
    lua_state.reset(L);

    luaL_openlibs(L);
}

void
LbInstance::RunLuaFile(const char *path)
{
    if (!lua_state)
        EnableLua();

    Lua::RunFile(lua_state.get(), path);
}

LbLua::LbLua(const char *path)
    :state(luaL_newstate())
{
    luaL_openlibs(state.get());
    Lua::RunFile(state.get(), path);
}
