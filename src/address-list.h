/*
 * Store a URI along with a list of socket addresses.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_ADDRESS_LIST_H
#define BENG_PROXY_ADDRESS_LIST_H

#include <inline/list.h>

#include <sys/socket.h>
#include <stdbool.h>

struct pool;

struct address_list {
    struct list_head addresses;
};

static inline void
address_list_init(struct address_list *al)
{
    list_init(&al->addresses);
}

void
address_list_copy(struct pool *pool, struct address_list *dest,
                  const struct address_list *src);

void
address_list_add(struct pool *pool, struct address_list *al,
                 const struct sockaddr *address, socklen_t length);

const struct sockaddr *
address_list_first(const struct address_list *al, socklen_t *length_r);

const struct sockaddr *
address_list_next(struct address_list *al, socklen_t *length_r);

/**
 * Is there no more than one address?
 */
bool
address_list_is_single(const struct address_list *al);

/**
 * Generates a unique string which identifies this object in a hash
 * table.  This string stored in a statically allocated buffer.
 */
const char *
address_list_key(const struct address_list *al);

#endif
