/*
 * Convert an input and an output pipe to a duplex socket.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef __BENG_DUPLEX_H
#define __BENG_DUPLEX_H

#include "pool.h"

int
duplex_new(pool_t pool, int read_fd, int write_fd);

#endif
