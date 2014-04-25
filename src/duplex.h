/*
 * Convert an input and an output pipe to a duplex socket.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef __BENG_DUPLEX_H
#define __BENG_DUPLEX_H

struct pool;

#ifdef __cplusplus
extern "C" {
#endif

int
duplex_new(struct pool *pool, int read_fd, int write_fd);

#ifdef __cplusplus
}
#endif

#endif
