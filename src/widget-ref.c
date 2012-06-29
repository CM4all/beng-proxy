/*
 * Widget reference functions.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "widget.h"
#include "pool.h"

#include <string.h>

const struct widget_ref *
widget_ref_parse(struct pool *pool, const char *_p)
{
    char *p, *slash;
    const struct widget_ref *root = NULL, **wr_p = &root;
    struct widget_ref *wr;

    if (_p == NULL || *_p == 0)
        return NULL;

    p = p_strdup(pool, _p);

    while ((slash = strchr(p, WIDGET_REF_SEPARATOR)) != NULL) {
        if (slash == p) {
            ++p;
            continue;
        }

        *slash = 0;
        wr = p_malloc(pool, sizeof(*wr));
        wr->next = NULL;
        wr->id = p;

        *wr_p = wr;
        wr_p = &wr->next;

        p = slash + 1;
    }

    if (*p != 0) {
        wr = p_malloc(pool, sizeof(*wr));
        wr->next = NULL;
        wr->id = p;
        *wr_p = wr;
    }

    return root;
}

bool
widget_ref_includes(const struct widget_ref *outer,
                    const struct widget_ref *inner)
{
    assert(inner != NULL);

    while (true) {
        if (strcmp(outer->id, inner->id) != 0)
            return false;

        outer = outer->next;
        if (outer == NULL)
            return true;

        inner = inner->next;
        if (inner == NULL)
            return false;
    }
}
