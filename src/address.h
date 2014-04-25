/*
 * Socket address utilities.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef __BENG_ADDRESS_H
#define __BENG_ADDRESS_H

#include <inline/compiler.h>

#include <stddef.h>

struct pool;
struct sockaddr;

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Converts a sockaddr into a human-readable string in the form
 * "IP:PORT".
 */
gcc_pure
const char *
address_to_string(struct pool *pool,
                  const struct sockaddr *addr, size_t addrlen);

/**
 * Converts a sockaddr into a human-readable string containing the
 * numeric IP address, ignoring the port number.
 */
gcc_pure
const char *
address_to_host_string(struct pool *pool, const struct sockaddr *address,
                       size_t address_length);

#ifdef __cplusplus
}
#endif

#endif
