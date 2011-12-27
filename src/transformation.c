/*
 * Utilities for the translate.c data structures.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "transformation.h"
#include "processor.h"

#include <string.h>

bool
transformation_is_container(const struct transformation *t)
{
    for (; t != NULL; t = t->next)
        if (t->type == TRANSFORMATION_PROCESS)
            return (t->u.processor.options & PROCESSOR_CONTAINER) != 0;

    return false;
}

struct transformation *
transformation_dup(struct pool *pool, const struct transformation *src)
{
    struct transformation *dest = p_malloc(pool, sizeof(*dest));

    dest->type = src->type;
    switch (dest->type) {
    case TRANSFORMATION_PROCESS:
        dest->u.processor.options = src->u.processor.options;
        break;

    case TRANSFORMATION_PROCESS_CSS:
        dest->u.css_processor.options = src->u.css_processor.options;
        break;

    case TRANSFORMATION_PROCESS_TEXT:
        break;

    case TRANSFORMATION_FILTER:
        resource_address_copy(pool, &dest->u.filter,
                              &src->u.filter);
        break;
    }

    dest->next = NULL;
    return dest;
}

struct transformation *
transformation_dup_chain(struct pool *pool, const struct transformation *src)
{
    struct transformation *dest = NULL, **tail_p = &dest;

    for (; src != NULL; src = src->next) {
        struct transformation *p = transformation_dup(pool, src);
        *tail_p = p;
        tail_p = &p->next;
    }

    return dest;
}

bool
transformation_any_is_expandable(const struct transformation *transformation)
{
    while (transformation != NULL) {
        if (transformation_is_expandable(transformation))
            return true;

        transformation = transformation->next;
    }

    return false;
}

void
transformation_expand(struct pool *pool, struct transformation *transformation,
                      const GMatchInfo *match_info)
{
    assert(pool != NULL);
    assert(transformation != NULL);
    assert(match_info != NULL);

    switch (transformation->type) {
    case TRANSFORMATION_PROCESS:
    case TRANSFORMATION_PROCESS_CSS:
    case TRANSFORMATION_PROCESS_TEXT:
        break;

    case TRANSFORMATION_FILTER:
        resource_address_expand(pool, &transformation->u.filter, match_info);
        break;
    }
}

void
transformation_expand_all(struct pool *pool,
                          struct transformation *transformation,
                          const GMatchInfo *match_info)
{
    assert(pool != NULL);
    assert(match_info != NULL);

    while (transformation != NULL) {
        transformation_expand(pool, transformation, match_info);
        transformation = transformation->next;
    }
}
