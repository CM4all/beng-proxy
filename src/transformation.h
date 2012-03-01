/*
 * Transformations which can be applied to resources.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_TRANSFORMATION_H
#define BENG_TRANSFORMATION_H

#include "resource-address.h"

#include <inline/compiler.h>

#include <glib.h>
#include <assert.h>

struct pool;

struct transformation {
    struct transformation *next;

    enum {
        TRANSFORMATION_PROCESS,
        TRANSFORMATION_PROCESS_CSS,
        TRANSFORMATION_PROCESS_TEXT,
        TRANSFORMATION_FILTER,
    } type;

    union {
        struct {
            unsigned options;
        } processor;

        struct {
            unsigned options;
        } css_processor;

        struct resource_address filter;
    } u;
};

/**
 * Returns true if the chain contains at least one "PROCESS"
 * transformation.
 */
gcc_pure
bool
transformation_has_processor(const struct transformation *t);

/**
 * Returns true if the first "PROCESS" transformation in the chain (if
 * any) includes the "CONTAINER" processor option.
 */
gcc_pure
bool
transformation_is_container(const struct transformation *t);

gcc_malloc
struct transformation *
transformation_dup(struct pool *pool, const struct transformation *src);

gcc_malloc
struct transformation *
transformation_dup_chain(struct pool *pool, const struct transformation *src);

/**
 * Does this transformation need to be expanded with
 * transformation_expand()?
 */
gcc_pure
static inline bool
transformation_is_expandable(const struct transformation *transformation)
{
    assert(transformation != NULL);

    return transformation->type == TRANSFORMATION_FILTER &&
        resource_address_is_expandable(&transformation->u.filter);
}

/**
 * Does any transformation in the linked list need to be expanded with
 * transformation_expand()?
 */
gcc_pure
bool
transformation_any_is_expandable(const struct transformation *transformation);

/**
 * Expand the strings in this transformation (not following the linked
 * lits) with the specified regex result.
 */
bool
transformation_expand(struct pool *pool, struct transformation *transformation,
                      const GMatchInfo *match_info, GError **error_r);

/**
 * The same as transformation_expand(), but expand all transformations
 * in the linked list.
 */
bool
transformation_expand_all(struct pool *pool,
                          struct transformation *transformation,
                          const GMatchInfo *match_info, GError **error_r);

#endif
