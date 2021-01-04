/*
 * Copyright 2007-2021 CM4all GmbH
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

#include "LuaHandler.hxx"
#include "LuaGoto.hxx"
#include "GotoConfig.hxx"
#include "Goto.hxx"
#include "http/IncomingRequest.hxx"
#include "HttpResponseHandler.hxx"
#include "pool/pool.hxx"
#include "lua/RunFile.hxx"
#include "lua/Util.hxx"
#include "lua/Assert.hxx"
#include "lua/Class.hxx"
#include "lua/Error.hxx"
#include "lua/InitHook.hxx"
#include "util/RuntimeError.hxx"
#include "util/ScopeExit.hxx"

extern "C" {
#include <lauxlib.h>
#include <lualib.h>
}

struct LbLuaRequestData {
	IncomingHttpRequest &request;
	HttpResponseHandler &handler;
	bool stale = false;

	explicit LbLuaRequestData(IncomingHttpRequest &_request,
				  HttpResponseHandler &_handler)
		:request(_request), handler(_handler) {}
};

static constexpr char lua_request_class[] = "lb.http_request";
typedef Lua::Class<LbLuaRequestData, lua_request_class> LbLuaRequest;

static LbLuaRequestData *
NewLuaRequest(lua_State *L, IncomingHttpRequest &request,
	      HttpResponseHandler &handler)
{
	return LbLuaRequest::New(L, request, handler);
}

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

	http_status_t status = HTTP_STATUS_OK;
	const char *msg;

	unsigned i = 2;

	if (top > 2) {
		if (!lua_isnumber(L, i))
			return luaL_argerror(L, i, "Integer status expected");

		status = http_status_t(lua_tointeger(L, i));
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
	Lua::SetField(L, -2, "resolve_connect", Lua::StackIndex(2));
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
		if (strcmp(i->name, name) == 0) {
			Lua::Push(L, i->func);
			return 1;
		}
	}

	if (strcmp(name, "uri") == 0) {
		Lua::Push(L, data.request.uri);
		return 1;
	} else if (strcmp(name, "method") == 0) {
		Lua::Push(L, http_method_to_string(data.request.method));
		return 1;
	} else if (strcmp(name, "has_body") == 0) {
		Lua::Push(L, data.request.HasBody());
		return 1;
	} else if (strcmp(name, "remote_host") == 0) {
		Lua::Push(L, data.request.remote_host);
		return 1;
	}

	return luaL_error(L, "Unknown attribute");
}

LbLuaHandler::LbLuaHandler(LuaInitHook &init_hook,
			   const LbLuaHandlerConfig &_config)
	:config(_config),
	 state(luaL_newstate()), function(state.get())
{
	auto *L = state.get();
	const Lua::ScopeCheckStack check_stack(L);

	luaL_openlibs(L);

	init_hook.PreInit(L);

	Lua::RunFile(L, config.path.c_str());

	init_hook.PostInit(L);

	lua_getglobal(L, config.function.c_str());
	AtScopeExit(L) { lua_pop(L, 1); };

	if (!lua_isfunction(L, -1)) {
		if (lua_isnil(L, -1))
			throw FormatRuntimeError("No such function: '%s' in %s",
						 config.function.c_str(),
						 config.path.c_str());
		else
			throw FormatRuntimeError("Not a function: '%s' in %s",
						 config.function.c_str(),
						 config.path.c_str());
	}

	function.Set(Lua::StackIndex(-2));

	LbLuaRequest::Register(L);
	Lua::SetTable(L, -3, "__index", LbLuaRequestIndex);
	lua_pop(L, 1);
}

LbLuaHandler::~LbLuaHandler()
{
}

const LbGoto *
LbLuaHandler::HandleRequest(IncomingHttpRequest &request,
			    HttpResponseHandler &handler)
{
	auto *L = state.get();
	const Lua::ScopeCheckStack check_stack(L);

	function.Push();
	auto *data = NewLuaRequest(L, request, handler);
	AtScopeExit(data) { data->stale = true; };

	if (lua_pcall(L, 1, 1, 0))
		throw Lua::PopError(L);

	AtScopeExit(L) { lua_pop(L, 1); };

	if (lua_isnil(L, -1))
		return nullptr;

	const auto *g = CheckLuaGoto(L, -1);
	if (g != nullptr)
		return g;

	if (lua_istable(L, -1)) {
		lua_getfield(L, -1, "resolve_connect");
		const char *resolve_connect = lua_tostring(L, -1);
		if (resolve_connect != nullptr) {
			/* allocate a LbGoto instance from the request pool */
			auto *rg = NewFromPool<LbGoto>(request.pool);
			rg->resolve_connect = p_strdup(request.pool, resolve_connect);
			lua_pop(L, 1);
			return rg;
		}

		lua_pop(L, 1);
	}

	throw std::runtime_error("Wrong return type from Lua handler");
}
