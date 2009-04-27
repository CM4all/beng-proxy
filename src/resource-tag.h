/*
 * A tag which addresses a resource in the filter cache.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_RESOURCE_TAG_H
#define BENG_RESOURCE_TAG_H

#include "pool.h"

struct strmap;

const char *
resource_tag_append_etag(pool_t pool, const char *tag,
                         const struct strmap *headers);

#endif
