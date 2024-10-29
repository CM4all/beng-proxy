// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#pragma once

struct lua_State;
struct IncomingHttpRequest;
struct LbHttpConnection;
class HttpResponseHandler;

struct LbLuaRequestData {
	const LbHttpConnection &connection;
	IncomingHttpRequest &request;
	HttpResponseHandler &handler;
	bool stale = false;

	explicit LbLuaRequestData(const LbHttpConnection &_connection,
				  IncomingHttpRequest &_request,
				  HttpResponseHandler &_handler) noexcept
		:connection(_connection), request(_request), handler(_handler) {}
};

void
RegisterLuaRequest(lua_State *L);

LbLuaRequestData *
NewLuaRequest(lua_State *L, const LbHttpConnection &connection,
	      IncomingHttpRequest &request,
	      HttpResponseHandler &handler);
