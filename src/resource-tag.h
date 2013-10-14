/*
 * A tag which addresses a resource in the filter cache.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_RESOURCE_TAG_H
#define BENG_RESOURCE_TAG_H

struct pool;
struct strmap;

#ifdef __cplusplus
extern "C" {
#endif

const char *
resource_tag_append_etag(struct pool *pool, const char *tag,
                         const struct strmap *headers);

#ifdef __cplusplus
}
#endif

#endif
