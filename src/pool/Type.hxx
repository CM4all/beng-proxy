// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#pragma once

#ifdef NDEBUG

#define TYPE_ARG_DECL
#define TYPE_ARG_FWD
#define TYPE_ARG_NULL
#define TYPE_ARG(T)

#else

#include <typeinfo>

#define TYPE_ARG_DECL , const char *type
#define TYPE_ARG_FWD , type
#define TYPE_ARG_NULL , nullptr
#define TYPE_ARG(T) , typeid(T).name()

#endif
