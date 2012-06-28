/*
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_STRSET_H
#define BENG_PROXY_STRSET_H

#include <inline/compiler.h>

#include <stdbool.h>
#include <stddef.h>

struct pool;

struct strset_item {
    struct strset_item *next;

    const char *value;
};

/**
 * An unordered set of strings.
 */
struct strset {
    struct strset_item *head;
};

#define strset_for_each_item(item, s) \
    for (const struct strset_item *item = s->head; item != NULL; item = item->next)

static inline void
strset_init(struct strset *s)
{
    s->head = NULL;
}

gcc_pure
static inline bool
strset_is_empty(const struct strset *s)
{
    return s->head == NULL;
}

gcc_pure
bool
strset_contains(const struct strset *s, const char *p);

/**
 * Add a string to the set.  It does not check whether the string
 * already exists.
 *
 * @param p the string value which must be allocated by the caller
 * @param pool a pool that is used to allocate the node (not the value)
 */
void
strset_add(struct pool *pool, struct strset *s, const char *p);

/**
 * Copy all strings from one set to the second, creating duplicates of
 * all values from the specified pool.
 */
void
strset_copy(struct pool *pool, struct strset *d, const struct strset *s);

#endif
