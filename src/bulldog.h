/*
 * Check the bulldog-tyke status directory.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_BULLDOG_H
#define BENG_PROXY_BULLDOG_H

#include "pool.h"

#include <stdbool.h>
#include <sys/socket.h>

/**
 * Initialized the bulldog-tyke library.
 *
 * @param path the bulldog status directory
 */
void
bulldog_init(const char *path);

void
bulldog_deinit(void);

/**
 * Returns true if the socket address is either not present in the
 * status directory, or if it is marked as "successful".
 */
bool
bulldog_check(const struct sockaddr *addr, socklen_t addrlen);

#endif
