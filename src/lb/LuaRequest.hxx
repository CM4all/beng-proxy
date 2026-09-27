// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <max.kellermann@ionos.com>

#pragma once

struct lua_State;
struct IncomingHttpRequest;
struct LbHttpConnection;
class HttpResponseHandler;

struct LbLuaRequestData {
	const LbHttpConnection &connection;
	IncomingHttpRequest &request;
	HttpResponseHandler &handler;

	/**
	 * Did the client send a request body?  #request.body has been
	 * moved away before the Lua script runs, so this needs to be
	 * remembered separately.
	 */
	const bool has_body;

	bool stale = false;

	explicit LbLuaRequestData(lua_State *L,
				  const LbHttpConnection &_connection,
				  IncomingHttpRequest &_request,
				  bool _has_body,
				  HttpResponseHandler &_handler) noexcept;
};

void
RegisterLuaRequest(lua_State *L);

LbLuaRequestData *
NewLuaRequest(lua_State *L, const LbHttpConnection &connection,
	      IncomingHttpRequest &request, bool has_body,
	      HttpResponseHandler &handler);
