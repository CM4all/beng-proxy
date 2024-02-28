// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#pragma once

struct lua_State;
struct IncomingHttpRequest;
class HttpResponseHandler;

struct LbLuaRequestData {
	IncomingHttpRequest &request;
	HttpResponseHandler &handler;
	bool stale = false;

	explicit LbLuaRequestData(IncomingHttpRequest &_request,
				  HttpResponseHandler &_handler)
		:request(_request), handler(_handler) {}
};

void
RegisterLuaRequest(lua_State *L);

LbLuaRequestData *
NewLuaRequest(lua_State *L, IncomingHttpRequest &request,
	      HttpResponseHandler &handler);
