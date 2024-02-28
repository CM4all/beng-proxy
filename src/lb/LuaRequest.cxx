// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#include "LuaRequest.hxx"
#include "pool/pool.hxx"
#include "lua/Class.hxx"
#include "lua/StackIndex.hxx"
#include "http/IncomingRequest.hxx"
#include "http/Method.hxx"
#include "http/ResponseHandler.hxx"
#include "http/Status.hxx"
#include "util/StringAPI.hxx"

extern "C" {
#include <lauxlib.h>
#include <lualib.h>
}

static constexpr char lua_request_class[] = "lb.http_request";
typedef Lua::Class<LbLuaRequestData, lua_request_class> LbLuaRequest;

static LbLuaRequestData &
CastLuaRequestData(lua_State *L, int idx)
{
	auto &data = LbLuaRequest::Cast(L, idx);
	if (data.stale)
		luaL_error(L, "Stale request");

	return data;
}

static int
GetHeader(lua_State *L)
{
	if (lua_gettop(L) != 2)
		return luaL_error(L, "Invalid parameters");

	auto &data = CastLuaRequestData(L, 1);

	if (!lua_isstring(L, 2))
		return luaL_argerror(L, 2, "String expected");

	const char *name = lua_tostring(L, 2);

	const char *value = data.request.headers.Get(name);
	if (value != nullptr) {
		Lua::Push(L, value);
		return 1;
	} else
		return 0;
}

static int
SendMessage(lua_State *L)
{
	const unsigned top = lua_gettop(L);
	if (top < 2 || top > 3)
		return luaL_error(L, "Invalid parameters");

	auto &data = CastLuaRequestData(L, 1);

	HttpStatus status = HttpStatus::OK;
	const char *msg;

	unsigned i = 2;

	if (top > 2) {
		if (!lua_isnumber(L, i))
			return luaL_argerror(L, i, "Integer status expected");

		status = static_cast<HttpStatus>(lua_tointeger(L, i));
		if (!http_status_is_valid(status))
			return luaL_argerror(L, i, "Invalid HTTP status");

		++i;
	}

	if (!lua_isstring(L, i))
		return luaL_argerror(L, i, "Message expected");

	msg = lua_tostring(L, i);

	if (http_status_is_empty(status))
		msg = nullptr;

	data.stale = true;
	data.handler.InvokeResponse(data.request.pool, status,
				    p_strdup(data.request.pool, msg));
	return 0;
}

static int
ResolveConnect(lua_State *L)
{
	if (lua_gettop(L) != 2)
		return luaL_error(L, "Invalid parameters");

	CastLuaRequestData(L, 1);

	if (!lua_isstring(L, 2))
		return luaL_argerror(L, 2, "String expected");

	lua_newtable(L);
	Lua::SetField(L, Lua::RelativeStackIndex{-1},
		      "resolve_connect", Lua::StackIndex(2));
	return 1;
}

static constexpr struct luaL_Reg request_methods [] = {
	{"get_header", GetHeader},
	{"send_message", SendMessage},
	{"resolve_connect", ResolveConnect},
	{nullptr, nullptr}
};

static int
LbLuaRequestIndex(lua_State *L)
{
	if (lua_gettop(L) != 2)
		return luaL_error(L, "Invalid parameters");

	auto &data = CastLuaRequestData(L, 1);

	if (!lua_isstring(L, 2))
		luaL_argerror(L, 2, "string expected");

	const char *name = lua_tostring(L, 2);

	for (const auto *i = request_methods; i->name != nullptr; ++i) {
		if (StringIsEqual(i->name, name)) {
			Lua::Push(L, i->func);
			return 1;
		}
	}

	if (StringIsEqual(name, "uri")) {
		Lua::Push(L, data.request.uri);
		return 1;
	} else if (StringIsEqual(name, "method")) {
		Lua::Push(L, http_method_to_string(data.request.method));
		return 1;
	} else if (StringIsEqual(name, "has_body")) {
		Lua::Push(L, data.request.HasBody());
		return 1;
	} else if (StringIsEqual(name, "remote_host")) {
		Lua::Push(L, data.request.remote_host);
		return 1;
	}

	return luaL_error(L, "Unknown attribute");
}

void
RegisterLuaRequest(lua_State *L)
{
	LbLuaRequest::Register(L);
	Lua::SetTable(L, Lua::RelativeStackIndex{-1},
		      "__index", LbLuaRequestIndex);
	lua_pop(L, 1);
}

LbLuaRequestData *
NewLuaRequest(lua_State *L, IncomingHttpRequest &request,
	      HttpResponseHandler &handler)
{
	return LbLuaRequest::New(L, request, handler);
}
