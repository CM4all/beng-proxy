/*
 * Remember which servers (socket addresses) failed recently.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef __BENG_FAILURE_H
#define __BENG_FAILURE_H

#include "pool.h"

#include <stdbool.h>
#include <stddef.h>

struct sockaddr;

void
failure_init(pool_t pool);

void
failure_deinit(void);

void
failure_add(const struct sockaddr *addr, size_t addrlen);

void
failure_remove(const struct sockaddr *addr, size_t addrlen);

/**
 * Returns true if the specified address has failed.
 */
bool
failure_check(const struct sockaddr *addr, size_t addrlen);

#endif
