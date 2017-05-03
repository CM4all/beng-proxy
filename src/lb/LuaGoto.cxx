/*
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "LuaGoto.hxx"
#include "lb_config.hxx"
#include "lua/Class.hxx"

static constexpr char lua_goto_class[] = "lb.goto";
typedef Lua::Class<LbGoto, lua_goto_class> LuaGoto;

static LbGoto &
CastLuaGoto(lua_State *L, int idx)
{
    return LuaGoto::Cast(L, idx);
}

static int
LuaGotoIndex(lua_State *L)
{
    if (lua_gettop(L) != 2)
        return luaL_error(L, "Invalid parameters");

    auto &g = CastLuaGoto(L, 1);

    if (!lua_isstring(L, 2))
        luaL_argerror(L, 2, "string expected");

    const char *name = lua_tostring(L, 2);
    if (strcmp(name, "type") == 0) {
        if (g.cluster != nullptr) {
            Lua::Push(L, "pool");
            return 1;
        } else if (g.branch != nullptr) {
            Lua::Push(L, "branch");
            return 1;
        } else if (g.lua != nullptr) {
            Lua::Push(L, "lua_handler");
            return 1;
        } else if (g.response.IsDefined()) {
            Lua::Push(L, "response");
            return 1;
        } else
            return 0;
    } else if (strcmp(name, "name") == 0) {
        if (g.cluster != nullptr) {
            Lua::Push(L, g.cluster->name.c_str());
            return 1;
        } else if (g.cluster != nullptr) {
            Lua::Push(L, g.branch->name.c_str());
            return 1;
        } else if (g.lua != nullptr) {
            Lua::Push(L, g.lua->name.c_str());
            return 1;
        }
    } else if (strcmp(name, "status") == 0) {
        if (g.response.IsDefined()) {
            Lua::Push(L, int(g.response.status));
            return 1;
        }
    } else if (strcmp(name, "location") == 0) {
        if (g.response.IsDefined()) {
            if (!g.response.location.empty()) {
                Lua::Push(L, g.response.location.c_str());
                return 1;
            }
        }
    } else if (strcmp(name, "message") == 0) {
        if (g.response.IsDefined()) {
            if (!g.response.message.empty()) {
                Lua::Push(L, g.response.message.c_str());
                return 1;
            }
        }
    }

    return luaL_error(L, "Unknown attribute");
}

void
RegisterLuaGoto(lua_State *L)
{
    LuaGoto::Register(L);
    Lua::SetTable(L, -3, "__index", LuaGotoIndex);
    lua_pop(L, 1);
}

LbGoto *
NewLuaGoto(lua_State *L, LbGoto &&src)
{
    return LuaGoto::New(L, std::move(src));
}

LbGoto &
CheckLuaGoto(lua_State *L, int idx)
{
    return *LuaGoto::Check(L, idx);
}
