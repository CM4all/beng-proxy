// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

/*
 * An access logger which passes data to a Lua script.
 */

#include "Server.hxx"
#include "Launch.hxx"
#include "lua/State.hxx"
#include "lua/Value.hxx"
#include "lua/RunFile.hxx"
#include "lua/Error.hxx"
#include "lua/Util.hxx"
#include "lib/fmt/RuntimeError.hxx"
#include "http/Method.hxx"
#include "net/FormatAddress.hxx"
#include "net/UniqueSocketDescriptor.hxx"
#include "net/log/ContentType.hxx"
#include "net/log/String.hxx"
#include "system/Error.hxx"
#include "time/Cast.hxx"
#include "util/PrintException.hxx"
#include "util/ScopeExit.hxx"
#include "util/StringAPI.hxx"
#include "util/ConstBuffer.hxx"

#include <functional>

#include <functional>

extern "C" {
#include <lauxlib.h>
#include <lualib.h>
}

using namespace Lua;

static void
LookupFunction(Lua::Value &dest, const char *path, const char *name)
{
	const auto L = dest.GetState();

	lua_getglobal(L, name);
	AtScopeExit(L) { lua_pop(L, 1); };

	if (!lua_isfunction(L, -1)) {
		if (lua_isnil(L, -1))
			throw FmtRuntimeError("No such function: '{}' in {}",
					      name, path);
		else
			throw FmtRuntimeError("Not a function: '{}' in {}",
					      name, path);
	}

	dest.Set(RelativeStackIndex{-1});
}

class LuaAccessLogger {
	Lua::Value function;

	/**
	 * Set the global variable "_"?  This is used for code fragments.
	 */
	bool set_underscore = false;

public:
	explicit LuaAccessLogger(lua_State *L)
		:function(L)
	{
	}

	void SetHandlerCode(const char *code) {
		const auto L = function.GetState();
		if (luaL_loadstring(L, code))
			throw Lua::PopError(L);

		AtScopeExit(L) { lua_pop(L, 1); };

		function.Set(RelativeStackIndex{-1});
		set_underscore = true;
	}

	void LoadFile(const char *path, const char *function_name) {
		const auto L = function.GetState();
		Lua::RunFile(L, path);
		LookupFunction(function, path, function_name);
	}

	void Handle(const ReceivedAccessLogDatagram &d,
		    SocketDescriptor filter_sink);
};

[[gnu::const]]
static const char *
TypeToString(Net::Log::Type type) noexcept
{
	return type == Net::Log::Type::UNSPECIFIED
		? nullptr
		: Net::Log::ToString(type);
}

void
LuaAccessLogger::Handle(const ReceivedAccessLogDatagram &d,
			SocketDescriptor filter_sink)
try {
	const auto L = function.GetState();

	function.Push(L);

	lua_newtable(L);

	if (!d.logger_client_address.IsNull() &&
	    d.logger_client_address.IsDefined()) {
		char buffer[1024];
		if (ToString(buffer, d.logger_client_address))
			Lua::SetTable(L, RelativeStackIndex{-1},
				      "logger_client", buffer);
	}

	if (d.HasTimestamp())
		SetTable(L, RelativeStackIndex{-1}, "timestamp",
			 ToFloatSeconds(d.timestamp.time_since_epoch()));

	if (d.remote_host != nullptr)
		SetTable(L, RelativeStackIndex{-1},
			 "remote_host", d.remote_host);

	if (d.host != nullptr)
		SetTable(L, RelativeStackIndex{-1}, "host", d.host);

	if (d.site != nullptr)
		SetTable(L, RelativeStackIndex{-1}, "site", d.site);

	if (d.analytics_id != nullptr)
		SetTable(L, RelativeStackIndex{-1}, "analytics_id", d.analytics_id);

	if (d.generator != nullptr)
		SetTable(L, RelativeStackIndex{-1}, "generator", d.generator);

	if (d.forwarded_to != nullptr)
		SetTable(L, RelativeStackIndex{-1},
			 "forwarded_to", d.forwarded_to);

	if (d.HasHttpMethod())
		SetTable(L, RelativeStackIndex{-1},
			 "http_method", http_method_to_string(d.http_method));

	if (d.http_uri != nullptr)
		SetTable(L, RelativeStackIndex{-1}, "http_uri", d.http_uri);

	if (d.http_referer != nullptr)
		SetTable(L, RelativeStackIndex{-1},
			 "http_referer", d.http_referer);

	if (d.user_agent != nullptr)
		SetTable(L, RelativeStackIndex{-1},
			 "user_agent", d.user_agent);

	if (d.message.data() != nullptr)
		SetTable(L, RelativeStackIndex{-1}, "message", d.message);

	if (d.HasHttpStatus())
		SetTable(L, RelativeStackIndex{-1},
			 "http_status", lua_Integer(d.http_status));

	if (d.valid_length)
		SetTable(L, RelativeStackIndex{-1},
			 "length", double(d.length));

	if (const auto content_type = ToString(d.content_type);
	    !content_type.empty())
		SetTable(L, RelativeStackIndex{-1},
			 "content_type", content_type);

	if (d.valid_traffic) {
		SetTable(L, RelativeStackIndex{-1},
			 "traffic_received", double(d.traffic_received));
		SetTable(L, RelativeStackIndex{-1},
			 "traffic_sent", double(d.traffic_sent));
	}

	if (d.valid_duration)
		SetTable(L, RelativeStackIndex{-1},
			 "duration", ToFloatSeconds(d.duration));

	if (const char *type = TypeToString(d.type))
		SetTable(L, RelativeStackIndex{-1}, "type", type);

	/* if the function is a Lua code fragment passed via
	   "--handler-code", then set the global variable "_" to the
	   request */
	if (set_underscore)
		SetGlobal(L, "_", RelativeStackIndex{-1});
	AtScopeExit(this, L) {
		if (set_underscore)
			Lua::SetGlobal(L, "_", nullptr);
	};

	if (lua_pcall(L, 1, filter_sink.IsDefined(), 0))
		throw Lua::PopError(L);

	if (filter_sink.IsDefined()) {
		const bool result = lua_toboolean(L, -1);
		lua_pop(L, 1);

		if (result)
			filter_sink.Write(d.raw);
	}
} catch (...) {
	PrintException(std::current_exception());
}

/**
 * An exception type which causes the usage text to be printed.
 */
struct Usage {};

int
main(int argc, char **argv)
try {
	ConstBuffer<const char *> args(argv + 1, argc - 1);

	const char *handler_code = nullptr;
	const char *path = nullptr, *function_name = nullptr;

	while (!args.empty() && args.front()[0] == '-') {
		if (StringIsEqual(args.front(), "--handler-code")) {
			args.shift();

			if (args.empty())
				throw Usage();

			handler_code = args.shift();
		} else if (StringIsEqual(args.front(), "--filter-exec"))
			break;
		else
			throw Usage();
	}

	if (handler_code == nullptr) {
		if (args.empty())
			throw Usage();

		path = args.shift();
		function_name = !args.empty() && args.front()[0] != '-'
			? args.shift()
			: "access_log";
	}

	UniqueSocketDescriptor filter_sink;

	if (!args.empty() && StringIsEqual(args.front(), "--filter-exec")) {
		args.shift();

		if (args.empty())
			throw Usage();

		filter_sink = LaunchLogger(args);
		args = {};
	}

	if (!args.empty())
		throw Usage();

	Lua::State state(luaL_newstate());
	luaL_openlibs(state.get());

	LuaAccessLogger logger(state.get());

	if (handler_code != nullptr)
		logger.SetHandlerCode(handler_code);
	else
		logger.LoadFile(path, function_name);

	AccessLogServer().Run(std::bind(&LuaAccessLogger::Handle, &logger,
					std::placeholders::_1,
					(SocketDescriptor)filter_sink));
	return EXIT_SUCCESS;
} catch (Usage) {
	fprintf(stderr, "Usage: %s {--handler-code CODE | FILE.lua [FUNCTION]} [--filter-exec PROGRAM ARGS...]\n", argv[0]);
	return EXIT_FAILURE;
} catch (...) {
	PrintException(std::current_exception());
	return EXIT_FAILURE;
}
