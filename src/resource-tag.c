/*
 * A tag which addresses a resource in the filter cache.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "resource-tag.h"
#include "strmap.h"
#include "http_util.h"
#include "pool.h"

const char *
resource_tag_append_etag(struct pool *pool, const char *tag,
                         const struct strmap *headers)
{
    const char *etag, *p;

    if (tag == NULL || headers == NULL)
        return NULL;

    etag = strmap_get(headers, "etag");
    if (etag == NULL)
        return NULL;

    p = strmap_get(headers, "cache-control");
    if (p != NULL && http_list_contains(p, "no-store"))
        /* generating a resource tag for the cache is pointless,
           because we are not allowed to store the response anyway */
        return NULL;

    return p_strcat(pool, tag, "|etag=", etag, NULL);
}

