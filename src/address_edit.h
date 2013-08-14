/*
 * Edit struct sockaddr objects.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_ADDRESS_EDIT_H
#define BENG_PROXY_ADDRESS_EDIT_H

#include <stddef.h>

struct pool;
struct sockaddr;

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Edits the sockaddr, sets a new TCP port.  If the object does not
 * need a modification, the original pointer may be returned.
 */
const struct sockaddr *
sockaddr_set_port(struct pool *pool,
                  const struct sockaddr *address, size_t length,
                  unsigned port);

#ifdef __cplusplus
}
#endif

#endif
