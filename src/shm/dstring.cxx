/*
 * String allocation for distributed pools.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "dpool.hxx"
#include "util/StringView.hxx"

#include <assert.h>
#include <string.h>

char *
d_memdup(struct dpool *pool, const void *src, size_t length)
{
    void *dest = d_malloc(pool, length);
    if (dest == nullptr)
        return nullptr;

    memcpy(dest, src, length);
    return (char *)dest;
}

char *
d_strdup(struct dpool *pool, const char *src)
{
    return (char *)d_memdup(pool, src, strlen(src) + 1);
}

char *
d_strdup(struct dpool *pool, StringView src)
{
    return d_strndup(pool, src.data, src.size);
}

char *
d_strndup(struct dpool *pool, const char *src, size_t length)
{
    char *dest = (char *)d_malloc(pool, length + 1);
    if (dest == nullptr)
        return nullptr;

    memcpy(dest, src, length);
    dest[length] = 0;
    return dest;
}

StringView
DupStringView(struct dpool &pool, StringView src)
{
    if (src.IsNull())
        return nullptr;

    if (src.IsEmpty())
        return StringView::Empty();

    const char *data = d_memdup(&pool, src.data, src.size);
    if (data == nullptr)
        /* out of memory */
        return nullptr;

    return {data, src.size};
}

void
FreeStringView(struct dpool &pool, StringView s)
{
    if (!s.IsEmpty())
        d_free(&pool, s.data);
}
