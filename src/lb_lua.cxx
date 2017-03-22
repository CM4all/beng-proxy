/*
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "lb_lua.hxx"
#include "lua/RunFile.hxx"

extern "C" {
#include <lauxlib.h>
#include <lualib.h>
}

LbLua::LbLua(const char *path)
    :state(luaL_newstate())
{
    luaL_openlibs(state.get());
    Lua::RunFile(state.get(), path);
}
