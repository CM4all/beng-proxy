/*
 * Copyright 2007-2017 Content Management AG
 * All rights reserved.
 *
 * author: Max Kellermann <mk.com>
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * - Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 *
 * - Redistributions in binary form must reproduce the above copyright
 * notice, this list of conditions and the following disclaimer in the
 * documentation and/or other materials provided with the
 * distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE
 * FOUNDATION OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED
 * OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "LuaGoto.hxx"
#include "LuaHandler.hxx"
#include "Goto.hxx"
#include "GotoConfig.hxx"
#include "Cluster.hxx"
#include "ClusterConfig.hxx"
#include "Branch.hxx"
#include "lua/Class.hxx"

static constexpr char lua_goto_class[] = "lb.goto";
typedef Lua::Class<LbGoto, lua_goto_class> LuaGoto;

static LbGoto &
CastLuaGoto(lua_State *L, int idx)
{
    return LuaGoto::Cast(L, idx);
}

static int
LuaGotoIndex(lua_State *L)
{
    if (lua_gettop(L) != 2)
        return luaL_error(L, "Invalid parameters");

    auto &g = CastLuaGoto(L, 1);

    if (!lua_isstring(L, 2))
        luaL_argerror(L, 2, "string expected");

    const char *name = lua_tostring(L, 2);
    if (strcmp(name, "type") == 0) {
        if (g.cluster != nullptr) {
            Lua::Push(L, "pool");
            return 1;
        } else if (g.branch != nullptr) {
            Lua::Push(L, "branch");
            return 1;
        } else if (g.lua != nullptr) {
            Lua::Push(L, "lua_handler");
            return 1;
        } else if (g.response != nullptr) {
            Lua::Push(L, "response");
            return 1;
        } else
            return 0;
    } else if (strcmp(name, "name") == 0) {
        if (g.cluster != nullptr) {
            Lua::Push(L, g.cluster->GetConfig().name.c_str());
            return 1;
        } else if (g.cluster != nullptr) {
            Lua::Push(L, g.branch->GetConfig().name.c_str());
            return 1;
        } else if (g.lua != nullptr) {
            Lua::Push(L, g.lua->GetConfig().name.c_str());
            return 1;
        }
    } else if (strcmp(name, "status") == 0) {
        if (g.response != nullptr) {
            Lua::Push(L, int(g.response->status));
            return 1;
        }
    } else if (strcmp(name, "location") == 0) {
        if (g.response != nullptr) {
            if (!g.response->location.empty()) {
                Lua::Push(L, g.response->location.c_str());
                return 1;
            }
        }
    } else if (strcmp(name, "message") == 0) {
        if (g.response != nullptr) {
            if (!g.response->message.empty()) {
                Lua::Push(L, g.response->message.c_str());
                return 1;
            }
        }
    }

    return luaL_error(L, "Unknown attribute");
}

void
RegisterLuaGoto(lua_State *L)
{
    LuaGoto::Register(L);
    Lua::SetTable(L, -3, "__index", LuaGotoIndex);
    lua_pop(L, 1);
}

LbGoto *
NewLuaGoto(lua_State *L, LbGoto &&src)
{
    return LuaGoto::New(L, std::move(src));
}

LbGoto *
CheckLuaGoto(lua_State *L, int idx)
{
    return LuaGoto::Check(L, idx);
}
