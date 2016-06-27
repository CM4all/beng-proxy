/*
 * A tag which addresses a resource in the filter cache.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "resource_tag.hxx"
#include "strmap.hxx"
#include "http_util.hxx"
#include "pool.hxx"

const char *
resource_tag_append_etag(struct pool *pool, const char *tag,
                         const StringMap *headers)
{
    const char *etag, *p;

    if (tag == NULL || headers == NULL)
        return NULL;

    etag = headers->Get("etag");
    if (etag == NULL)
        return NULL;

    p = headers->Get("cache-control");
    if (p != NULL && http_list_contains(p, "no-store"))
        /* generating a resource tag for the cache is pointless,
           because we are not allowed to store the response anyway */
        return NULL;

    return p_strcat(pool, tag, "|etag=", etag, NULL);
}

