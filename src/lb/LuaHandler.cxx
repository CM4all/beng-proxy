// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#include "LuaHandler.hxx"
#include "LuaGoto.hxx"
#include "LuaRequest.hxx"
#include "GotoConfig.hxx"
#include "Goto.hxx"
#include "http/IncomingRequest.hxx"
#include "pool/pool.hxx"
#include "lua/RunFile.hxx"
#include "lua/Util.hxx"
#include "lua/Assert.hxx"
#include "lua/Error.hxx"
#include "lua/InitHook.hxx"
#include "lib/fmt/RuntimeError.hxx"
#include "util/ScopeExit.hxx"

extern "C" {
#include <lauxlib.h>
#include <lualib.h>
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
			throw FmtRuntimeError("No such function: '{}' in {}",
					      config.function,
					      config.path.c_str());
		else
			throw FmtRuntimeError("Not a function: '{}' in {}",
					      config.function,
					      config.path.c_str());
	}

	function.Set(Lua::StackIndex(-2));

	RegisterLuaRequest(L);
}

LbLuaHandler::~LbLuaHandler() noexcept = default;

const LbGoto *
LbLuaHandler::HandleRequest(IncomingHttpRequest &request,
			    HttpResponseHandler &handler)
{
	auto *L = state.get();
	const Lua::ScopeCheckStack check_stack(L);

	function.Push(L);
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
		AtScopeExit(L) { lua_pop(L, 1); };

		const char *resolve_connect = lua_tostring(L, -1);
		if (resolve_connect != nullptr)
			/* allocate a LbGoto instance from the request pool */
			return NewFromPool<LbGoto>(request.pool, LbResolveConnect{p_strdup(request.pool, resolve_connect)});
	}

	throw std::runtime_error("Wrong return type from Lua handler");
}
