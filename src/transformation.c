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

static inline const char *
p_strdup_checked(pool_t pool, const char *s)
{
    return s == NULL ? NULL : p_strdup(pool, s);
}

struct transformation *
transformation_dup(pool_t pool, const struct transformation *src)
{
    struct transformation *dest = p_malloc(pool, sizeof(*dest));

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

struct transformation *
transformation_dup_chain(pool_t pool, const struct transformation *src)
{
    struct transformation *dest = NULL, **tail_p = &dest;

    for (; src != NULL; src = src->next) {
        struct transformation *p = transformation_dup(pool, src);
        *tail_p = p;
        tail_p = &p->next;
    }

    return dest;
}

const struct transformation_view *
transformation_view_lookup(const struct transformation_view *view,
                           const char *name)
{
    assert(view != NULL);
    assert(view->name == NULL);

    if (name == NULL)
        /* the default view has no name */
        return view;

    for (view = view->next; view != NULL; view = view->next) {
        assert(view->name != NULL);

        if (strcmp(view->name, name) == 0)
            return view;
    }

    return NULL;
}

struct transformation_view *
transformation_dup_view(pool_t pool, const struct transformation_view *src)
{
    struct transformation_view *dest = p_malloc(pool, sizeof(*dest));

    dest->next = NULL;
    dest->name = src->name != NULL ? p_strdup(pool, src->name) : NULL;
    dest->transformation = transformation_dup_chain(pool, src->transformation);

    return dest;
}

struct transformation_view *
transformation_dup_view_chain(pool_t pool, const struct transformation_view *src)
{
    struct transformation_view *dest = NULL, **tail_p = &dest;

    assert(src != NULL);
    assert(src->name == NULL);

    for (; src != NULL; src = src->next) {
        struct transformation_view *p = transformation_dup_view(pool, src);
        *tail_p = p;
        tail_p = &p->next;
    }

    return dest;
}
