/*
 * An access logger which passes data to a Lua script.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "Server.hxx"
#include "Datagram.hxx"
#include "lua/State.hxx"
#include "lua/Value.hxx"
#include "lua/RunFile.hxx"
#include "lua/Error.hxx"
#include "lua/Util.hxx"
#include "util/PrintException.hxx"
#include "util/RuntimeError.hxx"
#include "util/ScopeExit.hxx"

extern "C" {
#include <lauxlib.h>
#include <lualib.h>
}

static void
LookupFunction(lua_State *L, Lua::Value &dest, const char *path, const char *name)
{
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
    lua_State *const L;
    Lua::Value function;

public:
    LuaAccessLogger(lua_State *_L, const char *path, const char *function_name)
        :L(_L), function(L)
    {
         Lua::RunFile(L, path);
         LookupFunction(L, function, path, function_name);
    }

    void Handle(const AccessLogDatagram &d);
};

void
LuaAccessLogger::Handle(const AccessLogDatagram &d)
try {
    function.Push();

    lua_newtable(L);

    if (d.valid_timestamp)
        Lua::SetTable(L, -3, "timestamp", d.timestamp / 1000000.);

    if (d.remote_host != nullptr)
        Lua::SetTable(L, -3, "remote_host", d.remote_host);

    if (d.host != nullptr)
        Lua::SetTable(L, -3, "host", d.host);

    if (d.site != nullptr)
        Lua::SetTable(L, -3, "site", d.site);

    if (d.valid_http_method)
        Lua::SetTable(L, -3, "http_method", http_method_to_string(d.http_method));

    if (d.http_uri != nullptr)
        Lua::SetTable(L, -3, "http_uri", d.http_uri);

    if (d.http_referer != nullptr)
        Lua::SetTable(L, -3, "http_referer", d.http_referer);

    if (d.user_agent != nullptr)
        Lua::SetTable(L, -3, "user_agent", d.user_agent);

    if (d.valid_http_status)
        Lua::SetTable(L, -3, "http_status", int(d.http_status));

    if (d.valid_length)
        Lua::SetTable(L, -3, "length", double(d.length));

    if (d.valid_traffic) {
        Lua::SetTable(L, -3, "traffic_received", double(d.traffic_received));
        Lua::SetTable(L, -3, "traffic_sent", double(d.traffic_sent));
    }

    if (d.valid_duration)
        Lua::SetTable(L, -3, "duration", d.duration / 1000000.);

    if (lua_pcall(L, 1, 0, 0))
        throw Lua::PopError(L);
} catch (...) {
    PrintException(std::current_exception());
}

int
main(int argc, char **argv)
try {
    if (argc != 2 && argc != 3) {
        fprintf(stderr, "Usage: %s FILE.lua [FUNCTION]\n", argv[0]);
        return EXIT_FAILURE;
    }

    const char *const path = argv[1];
    const char *const function_name = argc >= 3 ? argv[2] : "access_log";

    Lua::State state(luaL_newstate());
    luaL_openlibs(state.get());

    LuaAccessLogger logger(state.get(), path, function_name);

    AccessLogServer(0).Run(std::bind(&LuaAccessLogger::Handle, &logger,
                                     std::placeholders::_1));
    return EXIT_SUCCESS;
} catch (...) {
    PrintException(std::current_exception());
    return EXIT_FAILURE;
}
