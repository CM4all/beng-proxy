/*
 * Widget reference functions.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "widget.h"

#include <string.h>

const struct widget_ref *
widget_ref_parse(pool_t pool, const char *_p)
{
    char *p, *slash;
    const struct widget_ref *root = NULL, **wr_p = &root;
    struct widget_ref *wr;

    if (_p == NULL || *_p == 0)
        return NULL;

    p = p_strdup(pool, _p);

    while ((slash = strchr(p, '/')) != NULL) {
        if (slash == p)
            continue;

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
