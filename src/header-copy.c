/*
 * Copy headers from one strmap to another.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "header-copy.h"
#include "strmap.h"

#include <assert.h>

void
header_copy_one(const struct strmap *in, struct strmap *out, const char *key)
{
    assert(in != NULL);
    assert(out != NULL);
    assert(key != NULL);

    const char *value = strmap_get(in, key);
    while (value != NULL) {
        strmap_add(out, key, value);
        value = strmap_get_next(in, key, value);
    }
}

void
header_copy_list(const struct strmap *in, struct strmap *out,
                 const char *const*keys)
{
    assert(in != NULL);
    assert(out != NULL);
    assert(keys != NULL);

    for (; *keys != NULL; ++keys)
        header_copy_one(in, out, *keys);
}
