// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#pragma once

struct lua_State;
struct LbGoto;

void
RegisterLuaGoto(lua_State *L);

LbGoto *
NewLuaGoto(lua_State *L, LbGoto &&src);

LbGoto *
CheckLuaGoto(lua_State *L, int idx);
