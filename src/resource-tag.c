/*
 * A tag which addresses a resource in the filter cache.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "resource-tag.h"
#include "strmap.h"

const char *
resource_tag_append_etag(pool_t pool, const char *tag,
                         const struct strmap *headers)
{
    const char *etag;

    if (tag == NULL || headers == NULL)
        return NULL;

    etag = strmap_get(headers, "etag");
    if (etag == NULL)
        return NULL;

    return p_strcat(pool, tag, "|etag=", etag, NULL);
}

