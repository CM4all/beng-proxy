// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#pragma once

#include "lua/State.hxx"
#include "lua/Value.hxx"

struct LbGoto;
struct LbLuaHandlerConfig;
struct IncomingHttpRequest;
class HttpResponseHandler;
class LuaInitHook;

class LbLuaHandler final {
	const LbLuaHandlerConfig &config;

	Lua::State state;
	Lua::Value function;

public:
	LbLuaHandler(LuaInitHook &init_hook, const LbLuaHandlerConfig &config);
	~LbLuaHandler();

	const LbLuaHandlerConfig &GetConfig() const {
		return config;
	}

	const LbGoto *HandleRequest(IncomingHttpRequest &request,
				    HttpResponseHandler &handler);
};
