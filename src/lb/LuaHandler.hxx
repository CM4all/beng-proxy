// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <max.kellermann@ionos.com>

#pragma once

#include "lua/State.hxx"
#include "lua/Value.hxx"

struct pool;
struct LbGoto;
struct LbLuaHandlerConfig;
struct IncomingHttpRequest;
class EventLoop;
class HttpResponseHandler;
class LuaInitHook;

class LbLuaHandler final {
	const LbLuaHandlerConfig &config;

	Lua::State state;
	Lua::Value function;

public:
	LbLuaHandler(EventLoop &event_loop,
		     LuaInitHook &init_hook, const LbLuaHandlerConfig &config);
	~LbLuaHandler() noexcept;

	const LbLuaHandlerConfig &GetConfig() const {
		return config;
	}

	lua_State *GetMainState() const noexcept {
		return state.get();
	}

	void PushFunction(lua_State *L) {
		function.Push(L);
	}

	const LbGoto *Finish(lua_State *L, struct pool &pool);
};
