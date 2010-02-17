/*
 * Transformations which can be applied to resources.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_TRANSFORMATION_H
#define BENG_TRANSFORMATION_H

#include "resource-address.h"

struct transformation {
    struct transformation *next;

    enum {
        TRANSFORMATION_PROCESS,
        TRANSFORMATION_FILTER,
    } type;

    union {
        struct {
            unsigned options;
        } processor;

        struct resource_address filter;
    } u;
};

struct transformation_view {
    struct transformation_view *next;

    /**
     * the name of this view; always NULL for the first (default) view
     */
    const char *name;

    struct transformation *transformation;
};

/**
 * Returns true if the first "PROCESS" transformation in the chain (if
 * any) includes the "CONTAINER" processor option.
 */
bool
transformation_is_container(const struct transformation *t);

struct transformation *
transformation_dup(pool_t pool, const struct transformation *src);

struct transformation *
transformation_dup_chain(pool_t pool, const struct transformation *src);

/**
 * Finds a view by its name.  If name==NULL, it returns the first
 * view.
 */
const struct transformation_view *
transformation_view_lookup(const struct transformation_view *view,
                           const char *name);

/**
 * Duplicate one transformation_view struct.
 */
struct transformation_view *
transformation_dup_view(pool_t pool, const struct transformation_view *src);

/**
 * Duplicate all transformation_view structs in the linked list.
 */
struct transformation_view *
transformation_dup_view_chain(pool_t pool, const struct transformation_view *src);

#endif
