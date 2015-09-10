/*
 * Check the bulldog-tyke status directory.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_BULLDOG_H
#define BENG_PROXY_BULLDOG_H

#include <inline/compiler.h>

#include <stdbool.h>
#include <sys/socket.h>

#ifdef __cplusplus
extern "C" {
#endif

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
gcc_pure
bool
bulldog_check(const struct sockaddr *addr, socklen_t addrlen);

/**
 * Returns true if the socket address is currently in "graceful"
 * shutdown.
 */
gcc_pure
bool
bulldog_is_fading(const struct sockaddr *address, socklen_t address_size);

#ifdef __cplusplus
}
#endif

#endif
