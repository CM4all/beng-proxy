/*
 * Socket address utilities.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef __BENG_ADDRESS_H
#define __BENG_ADDRESS_H

#include "pool.h"

#include <sys/socket.h>

const char *
address_to_string(pool_t pool, const struct sockaddr *addr, socklen_t addrlen);

#endif
