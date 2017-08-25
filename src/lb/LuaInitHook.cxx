/*
 * Copyright 2007-2017 Content Management AG
 * All rights reserved.
 *
 * author: Max Kellermann <mk@cm4all.com>
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

#include "LuaInitHook.hxx"
#include "LuaGoto.hxx"
#include "GotoMap.hxx"
#include "Goto.hxx"
#include "lua/Util.hxx"
#include "lua/Class.hxx"
#include "lua/Assert.hxx"

static constexpr char lua_pools_class[] = "lb.pools";
typedef Lua::Class<LbLuaInitHook *, lua_pools_class> LuaPools;

static LbLuaInitHook &
CheckLuaPools(lua_State *L, int idx)
{
    return *LuaPools::Cast(L, idx);
}

int
LbLuaInitHook::GetPool(lua_State *L, const char *name)
{
    if (goto_map == nullptr)
        return 0;

    auto g = goto_map->GetInstance(name);
    if (!g.IsDefined())
        return 0;

    NewLuaGoto(L, std::move(g));
    return 1;
}

static int
LuaPoolsIndex(lua_State *L)
{
    if (lua_gettop(L) != 2)
        return luaL_error(L, "Invalid parameters");

    auto &hook = CheckLuaPools(L, 1);

    if (!lua_isstring(L, 2))
        luaL_argerror(L, 2, "string expected");

    const char *name = lua_tostring(L, 2);
    return hook.GetPool(L, name);
}

void
LbLuaInitHook::PreInit(lua_State *L)
{
    const Lua::ScopeCheckStack check_stack(L);

    RegisterLuaGoto(L);

    LuaPools::Register(L);
    Lua::SetTable(L, -3, "__index", LuaPoolsIndex);
    lua_pop(L, 1);

    Lua::SetGlobal(L, "pools", Lua::Lambda([this, L](){
                LuaPools::New(L, this);
            }));
}

void
LbLuaInitHook::PostInit(lua_State *L)
{
    const Lua::ScopeCheckStack check_stack(L);

    Lua::SetGlobal(L, "pools", nullptr);
}
