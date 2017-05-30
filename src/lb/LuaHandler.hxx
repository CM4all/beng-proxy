/*
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_LB_LUA_HANDLER_HXX
#define BENG_LB_LUA_HANDLER_HXX

#include "lua/State.hxx"
#include "lua/Value.hxx"

#include <map>
#include <string>

struct LbConfig;
struct LbGotoIfConfig;
struct LbBranchConfig;
struct LbGotoConfig;
struct LbListenerConfig;
struct LbLuaHandlerConfig;
struct HttpServerRequest;
struct HttpResponseHandler;
class LuaInitHook;

class LbLuaHandler final {
    Lua::State state;
    Lua::Value function;

public:
    LbLuaHandler(LuaInitHook &init_hook, const LbLuaHandlerConfig &config);
    ~LbLuaHandler();

    const LbGotoConfig *HandleRequest(HttpServerRequest &request,
                                      HttpResponseHandler &handler);
};

class LbLuaHandlerMap {
    std::map<std::string, LbLuaHandler> handlers;

public:
    void Scan(LuaInitHook &init_hook, const LbConfig &config);

    LbLuaHandler *Find(const std::string &name) {
        auto i = handlers.find(name);
        return i != handlers.end()
            ? &i->second
            : nullptr;
    }

private:
    void Scan(LuaInitHook &init_hook, const LbGotoIfConfig &config);
    void Scan(LuaInitHook &init_hook, const LbBranchConfig &config);
    void Scan(LuaInitHook &init_hook, const LbGotoConfig &g);
    void Scan(LuaInitHook &init_hook, const LbListenerConfig &config);

    void Scan(LuaInitHook &init_hook, const LbLuaHandlerConfig &config);
};

#endif
