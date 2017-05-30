/*
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_LB_LUA_GOTO_HXX
#define BENG_LB_LUA_GOTO_HXX

struct lua_State;
struct LbGotoConfig;

void
RegisterLuaGoto(lua_State *L);

LbGotoConfig *
NewLuaGoto(lua_State *L, LbGotoConfig &&src);

LbGotoConfig *
CheckLuaGoto(lua_State *L, int idx);

#endif
