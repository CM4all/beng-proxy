/*
 * Copyright 2007-2019 Content Management AG
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
#include "net/ToString.hxx"
#include "net/UniqueSocketDescriptor.hxx"
#include "net/log/String.hxx"
#include "system/Error.hxx"
#include "time/Cast.hxx"
#include "util/PrintException.hxx"
#include "util/RuntimeError.hxx"
#include "util/ScopeExit.hxx"
#include "util/ConstBuffer.hxx"

#include <functional>

#include <functional>

extern "C" {
#include <lauxlib.h>
#include <lualib.h>
}

static void
LookupFunction(Lua::Value &dest, const char *path, const char *name)
{
    const auto L = dest.GetState();

    lua_getglobal(L, name);
    AtScopeExit(L) { lua_pop(L, 1); };

    if (!lua_isfunction(L, -1)) {
        if (lua_isnil(L, -1))
            throw FormatRuntimeError("No such function: '%s' in %s",
                                     name, path);
        else
            throw FormatRuntimeError("Not a function: '%s' in %s",
                                     name, path);
    }

    dest.Set(Lua::StackIndex(-2));
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

        function.Set(Lua::StackIndex(-2));
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

gcc_const
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

    function.Push();

    lua_newtable(L);

    if (!d.logger_client_address.IsNull() &&
        d.logger_client_address.IsDefined()) {
        char buffer[1024];
        if (ToString(buffer, sizeof(buffer), d.logger_client_address))
            Lua::SetTable(L, -3, "logger_client", buffer);
    }

    if (d.HasTimestamp())
        Lua::SetTable(L, -3, "timestamp",
                      ToFloatSeconds(d.timestamp.time_since_epoch()));

    if (d.remote_host != nullptr)
        Lua::SetTable(L, -3, "remote_host", d.remote_host);

    if (d.host != nullptr)
        Lua::SetTable(L, -3, "host", d.host);

    if (d.site != nullptr)
        Lua::SetTable(L, -3, "site", d.site);

    if (d.forwarded_to != nullptr)
        Lua::SetTable(L, -3, "forwarded_to", d.forwarded_to);

    if (d.HasHttpMethod())
        Lua::SetTable(L, -3, "http_method", http_method_to_string(d.http_method));

    if (d.http_uri != nullptr)
        Lua::SetTable(L, -3, "http_uri", d.http_uri);

    if (d.http_referer != nullptr)
        Lua::SetTable(L, -3, "http_referer", d.http_referer);

    if (d.user_agent != nullptr)
        Lua::SetTable(L, -3, "user_agent", d.user_agent);

    if (d.message != nullptr)
        Lua::SetTable(L, -3, "message", d.message);

    if (d.HasHttpStatus())
        Lua::SetTable(L, -3, "http_status", int(d.http_status));

    if (d.valid_length)
        Lua::SetTable(L, -3, "length", double(d.length));

    if (d.valid_traffic) {
        Lua::SetTable(L, -3, "traffic_received", double(d.traffic_received));
        Lua::SetTable(L, -3, "traffic_sent", double(d.traffic_sent));
    }

    if (d.valid_duration)
        Lua::SetTable(L, -3, "duration", ToFloatSeconds(d.duration));

    if (const char *type = TypeToString(d.type))
        Lua::SetTable(L, -3, "type", type);

    /* if the function is a Lua code fragment passed via
       "--handler-code", then set the global variable "_" to the
       request */
    if (set_underscore)
        Lua::SetGlobal(L, "_", Lua::StackIndex(-1));
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
            filter_sink.Write(d.raw.data, d.raw.size);
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
        if (strcmp(args.front(), "--handler-code") == 0) {
            args.shift();

            if (args.empty())
                throw Usage();

            handler_code = args.shift();
        } else if (strcmp(args.front(), "--filter-exec") == 0)
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

    if (!args.empty() && strcmp(args.front(), "--filter-exec") == 0) {
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
