/*
 * Socket address utilities.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef __BENG_ADDRESS_H
#define __BENG_ADDRESS_H

#include "pool.h"

#include <sys/socket.h>

/**
 * Converts a sockaddr into a human-readable string in the form
 * "IP:PORT".
 */
const char *
address_to_string(pool_t pool, const struct sockaddr *addr, socklen_t addrlen);

#endif
