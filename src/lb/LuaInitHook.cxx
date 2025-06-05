// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <max.kellermann@ionos.com>

#include "LuaInitHook.hxx"
#include "LuaGoto.hxx"
#include "GotoMap.hxx"
#include "Goto.hxx"
#include "lua/Util.hxx"
#include "lua/Class.hxx"
#include "lua/Assert.hxx"
#include "lua/PushLambda.hxx"

using namespace Lua;

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

	if (lua_type(L, 2) != LUA_TSTRING)
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
	SetTable(L, RelativeStackIndex{-1}, "__index", LuaPoolsIndex);
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
