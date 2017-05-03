/*
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_LB_LUA_GOTO_HXX
#define BENG_LB_LUA_GOTO_HXX

struct lua_State;
struct LbGoto;

void
RegisterLuaGoto(lua_State *L);

LbGoto *
NewLuaGoto(lua_State *L, LbGoto &&src);

LbGoto *
CheckLuaGoto(lua_State *L, int idx);

#endif
