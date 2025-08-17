// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <max.kellermann@ionos.com>

#include "LuaGoto.hxx"
#include "LuaHandler.hxx"
#include "Goto.hxx"
#include "GotoConfig.hxx"
#include "Cluster.hxx"
#include "ClusterConfig.hxx"
#include "Branch.hxx"
#include "lua/Class.hxx"
#include "util/AlwaysFalse.hxx"
#include "util/StringAPI.hxx"

using namespace Lua;

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

	if (lua_type(L, 2) != LUA_TSTRING)
		luaL_argerror(L, 2, "string expected");

	const char *name = lua_tostring(L, 2);
	if (StringIsEqual(name, "type")) {
		return std::visit([L](const auto &value){
			using T = std::decay_t<decltype(value)>;

			if constexpr (std::is_same_v<T, LbCluster *>) {
				Lua::Push(L, "pool");
				return 1;
			} else if constexpr (std::is_same_v<T, LbBranch *>) {
				Lua::Push(L, "branch");
				return 1;
			} else if constexpr (std::is_same_v<T, LbLuaHandler *>) {
				Lua::Push(L, "lua_handler");
				return 1;
			} else if constexpr (std::is_same_v<T, const LbSimpleHttpResponse *>) {
				Lua::Push(L, "response");
				return 1;
			} else if constexpr (std::is_same_v<T, LbTranslationHandler *> ||
					     std::is_same_v<T, HttpServerRequestHandler *> ||
					     std::is_same_v<T, LbResolveConnect> ||
					     std::is_same_v<T, std::monostate>) {
				return 0;
			} else {
				static_assert(always_false_v<T>);
			}
		}, g.destination);
	} else if (StringIsEqual(name, "name")) {
		const std::string_view result = std::visit([](const auto &value) -> std::string_view {
			using T = std::decay_t<decltype(value)>;

			if constexpr (std::is_same_v<T, LbCluster *> ||
				      std::is_same_v<T, LbBranch *> ||
				      std::is_same_v<T, LbLuaHandler *>) {
				return value->GetConfig().name;
			} else if constexpr (std::is_same_v<T, const LbSimpleHttpResponse *> ||
					     std::is_same_v<T, LbTranslationHandler *> ||
					     std::is_same_v<T, HttpServerRequestHandler *> ||
					     std::is_same_v<T, LbResolveConnect> ||
					     std::is_same_v<T, std::monostate>) {
				return {};
			} else {
				static_assert(always_false_v<T>);
			}
		}, g.destination);

		if (result.data() != nullptr) {
			Lua::Push(L, result);
			return 1;
		}
	} else if (StringIsEqual(name, "status")) {
		if (auto *value = std::get_if<const LbSimpleHttpResponse *>(&g.destination)) {
			const auto &response = **value;
			Lua::Push(L, lua_Integer(response.status));
			return 1;
		}
	} else if (StringIsEqual(name, "location")) {
		if (auto *value = std::get_if<const LbSimpleHttpResponse *>(&g.destination)) {
			const auto &response = **value;
			if (!response.location.empty()) {
				Lua::Push(L, response.location);
				return 1;
			}
		}
	} else if (StringIsEqual(name, "message")) {
		if (auto *value = std::get_if<const LbSimpleHttpResponse *>(&g.destination)) {
			const auto &response = **value;
			if (!response.message.empty()) {
				Lua::Push(L, response.message);
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
	SetTable(L, RelativeStackIndex{-1}, "__index", LuaGotoIndex);
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
