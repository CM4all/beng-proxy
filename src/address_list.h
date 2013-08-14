/*
 * Store a URI along with a list of socket addresses.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_ADDRESS_LIST_H
#define BENG_PROXY_ADDRESS_LIST_H

#include "sticky.h"

#include <inline/compiler.h>

#include <stddef.h>
#include <stdbool.h>
#include <assert.h>

#define MAX_ADDRESSES 16

struct pool;
struct sockaddr;

struct address_list {
    enum sticky_mode sticky_mode;

    /** the number of addresses */
    unsigned size;

    struct address_envelope *addresses[MAX_ADDRESSES];
};

static inline void
address_list_init(struct address_list *list)
{
    list->sticky_mode = STICKY_NONE;
    list->size = 0;
}

static inline void
address_list_set_sticky_mode(struct address_list *list,
                             enum sticky_mode sticky_mode)
{
    list->sticky_mode = sticky_mode;
}

#ifdef __cplusplus
extern "C" {
#endif

void
address_list_copy(struct pool *pool, struct address_list *dest,
                  const struct address_list *src);

/**
 * @return false if the list is full
 */
bool
address_list_add(struct pool *pool, struct address_list *list,
                 const struct sockaddr *address, size_t length);

gcc_pure
static inline const struct address_envelope *
address_list_get_n(const struct address_list *list, unsigned n)
{
    assert(n < list->size);

    return list->addresses[n];
}

gcc_pure
const struct address_envelope *
address_list_first(const struct address_list *list);

gcc_pure
static inline bool
address_list_is_empty(const struct address_list *list)
{
    return list->size == 0;
}

/**
 * Is there no more than one address?
 */
gcc_pure
static inline bool
address_list_is_single(const struct address_list *list)
{
    return list->size == 1;
}

/**
 * Generates a unique string which identifies this object in a hash
 * table.  This string stored in a statically allocated buffer.
 */
gcc_pure
const char *
address_list_key(const struct address_list *list);

#ifdef __cplusplus
}
#endif

#endif
