/*
 * Copy headers from one strmap to another.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "header-copy.h"
#include "strmap.h"

#include <assert.h>
#include <string.h>

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

void
header_copy_prefix(struct strmap *in, struct strmap *out, const char *prefix)
{
    assert(in != NULL);
    assert(out != NULL);
    assert(prefix != NULL);
    assert(*prefix != 0);

    strmap_rewind(in);

    const size_t prefix_length = strlen(prefix);

    const struct strmap_pair *pair;
    while ((pair = strmap_next(in)) != NULL)
        if (memcmp(pair->key, prefix, prefix_length) == 0)
            strmap_add(out, pair->key, pair->value);
}
