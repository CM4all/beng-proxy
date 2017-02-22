/*
 * Check the bulldog-tyke status directory.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_BULLDOG_HXX
#define BENG_PROXY_BULLDOG_HXX

#include <inline/compiler.h>

class SocketAddress;

/**
 * Initialized the bulldog-tyke library.
 *
 * @param path the bulldog status directory
 */
void
bulldog_init(const char *path);

void
bulldog_deinit();

/**
 * Returns true if the socket address is either not present in the
 * status directory, or if it is marked as "successful".
 */
gcc_pure
bool
bulldog_check(SocketAddress address);

/**
 * Returns true if the socket address is currently in "graceful"
 * shutdown.
 */
gcc_pure
bool
bulldog_is_fading(SocketAddress address);

#endif
