// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <max.kellermann@ionos.com>

#include "LuaRequest.hxx"
#include "HttpConnection.hxx"
#include "istream/istream_string.hxx"
#include "pool/pool.hxx"
#include "lua/Class.hxx"
#include "lua/FenvCache.hxx"
#include "lua/StackIndex.hxx"
#include "lua/StringView.hxx"
#include "http/CommonHeaders.hxx"
#include "http/IncomingRequest.hxx"
#include "http/Method.hxx"
#include "http/ResponseHandler.hxx"
#include "http/Status.hxx"
#include "uri/Extract.hxx"
#include "uri/MapQueryString.hxx"
#include "uri/Verify.hxx"
#include "util/StringAPI.hxx"
#include "AllocatorPtr.hxx"

extern "C" {
#include <lauxlib.h>
#include <lualib.h>
}

using std::string_view_literals::operator""sv;

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

inline
LbLuaRequestData::LbLuaRequestData(lua_State *L,
				   const LbHttpConnection &_connection,
				   IncomingHttpRequest &_request,
				   HttpResponseHandler &_handler) noexcept
	:connection(_connection), request(_request), handler(_handler)
{
	lua_newtable(L);
	lua_setfenv(L, -2);
}

static int
GetHeader(lua_State *L)
{
	if (lua_gettop(L) != 2)
		return luaL_error(L, "Invalid parameters");

	auto &data = CastLuaRequestData(L, 1);

	if (lua_type(L, 2) != LUA_TSTRING) 
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

	if (lua_type(L, i) != LUA_TSTRING)
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
SendRedirect(lua_State *L)
{
	const unsigned top = lua_gettop(L);

	auto &data = CastLuaRequestData(L, 1);

	HttpStatus status = HttpStatus::FOUND;
	std::string_view msg{};

	unsigned i = 2;
	if (i > top)
		return luaL_error(L, "Not enough parameters");

	if (lua_isnumber(L, i)) {
		status = static_cast<HttpStatus>(lua_tointeger(L, i));
		if (!http_status_is_valid(status))
			return luaL_argerror(L, i, "Invalid HTTP status");

		if (!http_status_is_redirect(status))
			return luaL_argerror(L, i, "Invalid HTTP redirect status");

		++i;
		if (i > top)
			return luaL_error(L, "Not enough parameters");
	}

	if (lua_type(L, i) != LUA_TSTRING)
		return luaL_argerror(L, i, "URL expected");

	const auto location = Lua::ToStringView(L, i);
	if (!VerifyHttpUrl(location))
		return luaL_argerror(L, i, "Malformed URL");

	++i;
	if (i < top) {
		if (lua_type(L, i) != LUA_TSTRING)
			return luaL_argerror(L, i, "String expected");

		msg = Lua::ToStringView(L, i);
		++i;
	}

	if (i < top)
		return luaL_error(L, "Too many parameters");

	auto &pool = data.request.pool;
	const AllocatorPtr alloc{pool};

	StringMap response_headers;
	response_headers.Add(alloc, location_header, alloc.DupZ(location));

	UnusedIstreamPtr response_body;
	if (!msg.empty() && !http_status_is_empty(status)) {
		response_headers.Add(alloc, content_type_header, "text/plain");
		response_body = istream_string_new(pool, msg);
	}

	data.stale = true;
	data.handler.InvokeResponse(status,
				    std::move(response_headers),
				    std::move(response_body));
	return 0;
}

static int
SendRedirectHost(lua_State *L)
{
	const unsigned top = lua_gettop(L);

	auto &data = CastLuaRequestData(L, 1);

	HttpStatus status = HttpStatus::FOUND;
	std::string_view msg{};

	unsigned i = 2;
	if (i > top)
		return luaL_error(L, "Not enough parameters");

	if (lua_isnumber(L, i)) {
		status = static_cast<HttpStatus>(lua_tointeger(L, i));
		if (!http_status_is_valid(status))
			return luaL_argerror(L, i, "Invalid HTTP status");

		if (!http_status_is_redirect(status))
			return luaL_argerror(L, i, "Invalid HTTP redirect status");

		++i;
		if (i > top)
			return luaL_error(L, "Not enough parameters");
	}

	if (lua_type(L, i) != LUA_TSTRING)
		return luaL_argerror(L, i, "URL expected");

	const auto host = Lua::ToStringView(L, i);
	if (!VerifyUriHostPort(host))
		return luaL_argerror(L, i, "Malformed host");

	++i;
	if (i < top) {
		if (lua_type(L, i) != LUA_TSTRING)
			return luaL_argerror(L, i, "String expected");

		msg = Lua::ToStringView(L, i);
		++i;
	}

	if (i < top)
		return luaL_error(L, "Too many parameters");

	auto &pool = data.request.pool;
	const AllocatorPtr alloc{pool};

	StringMap response_headers;

	// TODO hard-coded scheme - is "https://" always correct?
	response_headers.Add(alloc, location_header,
			     alloc.Concat("https://"sv, host, data.request.uri));

	UnusedIstreamPtr response_body;
	if (!msg.empty() && !http_status_is_empty(status)) {
		response_headers.Add(alloc, content_type_header, "text/plain");
		response_body = istream_string_new(pool, msg);
	}

	data.stale = true;
	data.handler.InvokeResponse(status,
				    std::move(response_headers),
				    std::move(response_body));
	return 0;
}

static int
ResolveConnect(lua_State *L)
{
	if (lua_gettop(L) != 2)
		return luaL_error(L, "Invalid parameters");

	CastLuaRequestData(L, 1);

	if (lua_type(L, 2) != LUA_TSTRING)
		return luaL_argerror(L, 2, "String expected");

	lua_newtable(L);
	Lua::SetField(L, Lua::RelativeStackIndex{-1},
		      "resolve_connect", Lua::StackIndex(2));
	return 1;
}

static constexpr struct luaL_Reg request_methods [] = {
	{"get_header", GetHeader},
	{"send_message", SendMessage},
	{"send_redirect", SendRedirect},
	{"send_redirect_host", SendRedirectHost},
	{"resolve_connect", ResolveConnect},
	{nullptr, nullptr}
};

static int
LbLuaRequestIndex(lua_State *L)
{
	if (lua_gettop(L) != 2)
		return luaL_error(L, "Invalid parameters");

	auto &data = CastLuaRequestData(L, 1);

	if (lua_type(L, 2) != LUA_TSTRING)
		luaL_argerror(L, 2, "string expected");

	constexpr Lua::StackIndex name_idx{2};
	const char *name = lua_tostring(L, 2);

	for (const auto *i = request_methods; i->name != nullptr; ++i) {
		if (StringIsEqual(i->name, name)) {
			Lua::Push(L, i->func);
			return 1;
		}
	}

	// look it up in the fenv (our cache)
	if (Lua::GetFenvCache(L, 1, name_idx))
		return 1;

	if (StringIsEqual(name, "uri")) {
		Lua::Push(L, data.request.uri);

		// copy a reference to the fenv (our cache)
		Lua::SetFenvCache(L, 1, name_idx, Lua::RelativeStackIndex{-1});

		return 1;
	} else if (StringIsEqual(name, "query_table")) {
		lua_newtable(L);

		if (const char *const query = UriQuery(data.request.uri)) {
			for (const auto &[key, value] : MapQueryString(query)) {
				Lua::SetTable(L, Lua::RelativeStackIndex{-1}, key, value);
			}
		}

		// copy a reference to the fenv (our cache)
		Lua::SetFenvCache(L, 1, name_idx, Lua::RelativeStackIndex{-1});

		return 1;
	} else if (StringIsEqual(name, "method")) {
		Lua::Push(L, http_method_to_string(data.request.method));

		// copy a reference to the fenv (our cache)
		Lua::SetFenvCache(L, 1, name_idx, Lua::RelativeStackIndex{-1});

		return 1;
	} else if (StringIsEqual(name, "has_body")) {
		Lua::Push(L, data.request.HasBody());
		return 1;
	} else if (StringIsEqual(name, "remote_host")) {
		Lua::Push(L, data.request.remote_host);

		// copy a reference to the fenv (our cache)
		Lua::SetFenvCache(L, 1, name_idx, Lua::RelativeStackIndex{-1});

		return 1;
	} else if (StringIsEqual(name, "peer_subject")) {
		if (const char *value = data.connection.GetPeerSubject()) {
			Lua::Push(L, value);

			// copy a reference to the fenv (our cache)
			Lua::SetFenvCache(L, 1, name_idx, Lua::RelativeStackIndex{-1});

			return 1;
		}

		return 0;
	} else if (StringIsEqual(name, "peer_issuer_subject")) {
		if (const char *value = data.connection.GetPeerIssuerSubject()) {
			Lua::Push(L, value);

			// copy a reference to the fenv (our cache)
			Lua::SetFenvCache(L, 1, name_idx, Lua::RelativeStackIndex{-1});

			return 1;
		}

		return 0;
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
NewLuaRequest(lua_State *L, const LbHttpConnection &connection,
	      IncomingHttpRequest &request,
	      HttpResponseHandler &handler)
{
	return LbLuaRequest::New(L, L, connection, request, handler);
}
