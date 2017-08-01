/*
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_LB_LUA_HANDLER_HXX
#define BENG_LB_LUA_HANDLER_HXX

#include "lua/State.hxx"
#include "lua/Value.hxx"

struct LbGoto;
struct LbLuaHandlerConfig;
struct HttpServerRequest;
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

    const LbGoto *HandleRequest(HttpServerRequest &request,
                                HttpResponseHandler &handler);
};

#endif
