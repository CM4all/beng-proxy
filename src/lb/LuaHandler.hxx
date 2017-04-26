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
struct LbGoto;
struct LbListenerConfig;
struct LbLuaHandlerConfig;
struct HttpServerRequest;
struct HttpResponseHandler;

class LbLuaHandler final {
    Lua::State state;
    Lua::Value function;

public:
    LbLuaHandler(const LbLuaHandlerConfig &config);
    ~LbLuaHandler();

    void HandleRequest(HttpServerRequest &request,
                       HttpResponseHandler &handler);
};

class LbLuaHandlerMap {
    std::map<std::string, LbLuaHandler> handlers;

public:
    void Scan(const LbConfig &config);

    LbLuaHandler *Find(const std::string &name) {
        auto i = handlers.find(name);
        return i != handlers.end()
            ? &i->second
            : nullptr;
    }

private:
    void Scan(const LbGotoIfConfig &config);
    void Scan(const LbBranchConfig &config);
    void Scan(const LbGoto &g);
    void Scan(const LbListenerConfig &config);

    void Scan(const LbLuaHandlerConfig &config);
};

#endif
