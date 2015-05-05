/*
 * Temporary memory pool.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_TPOOL_HXX
#define BENG_PROXY_TPOOL_HXX

#include "pool.h"

extern struct pool *tpool;

void
tpool_init(struct pool *parent);

void
tpool_deinit();

#endif
