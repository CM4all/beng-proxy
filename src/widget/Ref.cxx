/*
 * Widget reference functions.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "Ref.hxx"
#include "pool.hxx"
#include "util/IterableSplitString.hxx"

#include <assert.h>
#include <string.h>

const struct widget_ref *
widget_ref_parse(struct pool *pool, const char *_p)
{
    const struct widget_ref *root = nullptr, **wr_p = &root;

    if (_p == nullptr || *_p == 0)
        return nullptr;

    for (auto id : IterableSplitString(_p, WIDGET_REF_SEPARATOR)) {
        if (id.IsEmpty())
            continue;

        auto wr = NewFromPool<struct widget_ref>(*pool);
        wr->next = nullptr;
        wr->id = p_strndup(pool, id.data, id.size);

        *wr_p = wr;
        wr_p = &wr->next;
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
