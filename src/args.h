/*
 * Parse the argument list in an URI after the semicolon.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef __BENG_ARGS_H
#define __BENG_ARGS_H

#include "pool.h"
#include "strmap.h"

strmap_t
args_parse(pool_t pool, const char *p);

#endif
