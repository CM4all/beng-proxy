/*
 * Convert an input and an output pipe to a duplex socket.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_DUPLEX_HXX
#define BENG_PROXY_DUPLEX_HXX

struct pool;

int
duplex_new(struct pool *pool, int read_fd, int write_fd);

#endif
