/*
 * Temporary memory pool.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef __BENG_TPOOL_H
#define __BENG_TPOOL_H

#include "pool.h"

extern struct pool *tpool;

#ifdef __cplusplus
extern "C" {
#endif

void
tpool_init(struct pool *parent);

void
tpool_deinit(void);

#ifdef __cplusplus
}
#endif

#endif
