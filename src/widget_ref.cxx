/*
 * Widget reference functions.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "widget.hxx"
#include "pool.hxx"

#include <string.h>

const struct widget_ref *
widget_ref_parse(struct pool *pool, const char *_p)
{
    char *p, *slash;
    const struct widget_ref *root = nullptr, **wr_p = &root;

    if (_p == nullptr || *_p == 0)
        return nullptr;

    p = p_strdup(pool, _p);

    while ((slash = strchr(p, WIDGET_REF_SEPARATOR)) != nullptr) {
        if (slash == p) {
            ++p;
            continue;
        }

        *slash = 0;
        auto wr = NewFromPool<struct widget_ref>(*pool);
        wr->next = nullptr;
        wr->id = p;

        *wr_p = wr;
        wr_p = &wr->next;

        p = slash + 1;
    }

    if (*p != 0) {
        auto wr = NewFromPool<struct widget_ref>(*pool);
        wr->next = nullptr;
        wr->id = p;
        *wr_p = wr;
    }

    return root;
}

bool
widget_ref_includes(const struct widget_ref *outer,
                    const struct widget_ref *inner)
{
    assert(inner != nullptr);

    while (true) {
        if (strcmp(outer->id, inner->id) != 0)
            return false;

        outer = outer->next;
        if (outer == nullptr)
            return true;

        inner = inner->next;
        if (inner == nullptr)
            return false;
    }
}
