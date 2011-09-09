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

enum failure_status {
    /**
     * No failure, host is ok.
     */
    FAILURE_OK,

    /**
     * Host has failed.
     */
    FAILURE_FAILED,
};

void
failure_init(pool_t pool);

void
failure_deinit(void);

void
failure_set(const struct sockaddr *address, size_t length,
            enum failure_status status, unsigned duration);

static inline void
failure_add(const struct sockaddr *address, size_t length)
{
    failure_set(address, length, FAILURE_FAILED, 20);
}

void
failure_remove(const struct sockaddr *addr, size_t addrlen);

enum failure_status
failure_get_status(const struct sockaddr *address, size_t length);

/**
 * Returns true if the specified address has failed.
 */
static inline bool
failure_check(const struct sockaddr *address, size_t length)
{
    return failure_get_status(address, length) != FAILURE_OK;
}

#endif
