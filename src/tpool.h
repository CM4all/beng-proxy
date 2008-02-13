/*
 * Temporary memory pool.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef __BENG_TPOOL_H
#define __BENG_TPOOL_H

#include "pool.h"

extern pool_t tpool;

void
tpool_init(pool_t parent);

void
tpool_deinit(void);

#endif
