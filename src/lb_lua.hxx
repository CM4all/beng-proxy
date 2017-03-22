/*
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_LB_LUA_HXX
#define BENG_LB_LUA_HXX

#include "lua/State.hxx"

class LbLua {
    Lua::State state;

public:
    explicit LbLua(const char *path);
};

#endif
