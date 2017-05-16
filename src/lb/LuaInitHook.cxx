/*
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "LuaInitHook.hxx"
#include "LuaGoto.hxx"
#include "ClusterMap.hxx"
#include "lb_config.hxx"
#include "lua/Util.hxx"
#include "lua/Class.hxx"
#include "lua/Assert.hxx"

static constexpr char lua_pools_class[] = "lb.pools";
typedef Lua::Class<LbLuaInitHook *, lua_pools_class> LuaPools;

static LbLuaInitHook &
CheckLuaPools(lua_State *L, int idx)
{
    return *LuaPools::Cast(L, idx);
}

int
LbLuaInitHook::GetPool(lua_State *L, const char *name)
{
    auto g = config.FindGoto(name);
    if (!g.IsDefined())
        return 0;

    if (clusters != nullptr)
        /* mark the referenced pools as "used" */
        clusters->Scan(g, *avahi_client);

    NewLuaGoto(L, std::move(g));
    return 1;
}

static int
LuaPoolsIndex(lua_State *L)
{
    if (lua_gettop(L) != 2)
        return luaL_error(L, "Invalid parameters");

    auto &hook = CheckLuaPools(L, 1);

    if (!lua_isstring(L, 2))
        luaL_argerror(L, 2, "string expected");

    const char *name = lua_tostring(L, 2);
    return hook.GetPool(L, name);
}

void
LbLuaInitHook::PreInit(lua_State *L)
{
    const Lua::ScopeCheckStack check_stack(L);

    RegisterLuaGoto(L);

    LuaPools::Register(L);
    Lua::SetTable(L, -3, "__index", LuaPoolsIndex);
    lua_pop(L, 1);

    Lua::SetGlobal(L, "pools", Lua::Lambda([this, L](){
                LuaPools::New(L, this);
            }));
}

void
LbLuaInitHook::PostInit(lua_State *L)
{
    const Lua::ScopeCheckStack check_stack(L);

    Lua::SetGlobal(L, "pools", nullptr);
}
