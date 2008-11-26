/*
 * Utilities for the translate.c data structures.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "transformation.h"

static inline const char *
p_strdup_checked(pool_t pool, const char *s)
{
    return s == NULL ? NULL : p_strdup(pool, s);
}

struct translate_transformation *
transformation_dup(pool_t pool, const struct translate_transformation *src)
{
    struct translate_transformation *dest = p_malloc(pool, sizeof(*dest));

    dest->type = src->type;
    switch (dest->type) {
    case TRANSFORMATION_PROCESS:
        dest->u.processor.options = src->u.processor.options;
        dest->u.processor.domain = p_strdup_checked(pool, src->u.processor.domain);
        break;

    case TRANSFORMATION_FILTER:
        resource_address_copy(pool, &dest->u.filter,
                              &src->u.filter);
        break;
    }

    dest->next = NULL;
    return dest;
}

struct translate_transformation *
transformation_dup_chain(pool_t pool, const struct translate_transformation *src)
{
    struct translate_transformation *dest = NULL, **tail_p = &dest;

    for (; src != NULL; src = src->next) {
        struct translate_transformation *p = transformation_dup(pool, src);
        *tail_p = p;
        tail_p = &p->next;
    }

    return dest;
}
