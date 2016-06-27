/*
 * A tag which addresses a resource in the filter cache.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_RESOURCE_TAG_HXX
#define BENG_PROXY_RESOURCE_TAG_HXX

struct pool;
class StringMap;

const char *
resource_tag_append_etag(struct pool *pool, const char *tag,
                         const StringMap *headers);

#endif
