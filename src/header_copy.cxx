/*
 * Copy headers from one strmap to another.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "header_copy.hxx"
#include "strmap.hxx"

#include <assert.h>
#include <string.h>

void
header_copy_one(const struct strmap *in, struct strmap *out, const char *key)
{
    assert(in != nullptr);
    assert(out != nullptr);
    assert(key != nullptr);

    for (const struct strmap_pair *pair = strmap_lookup_first(in, key);
         pair != nullptr; pair = strmap_lookup_next(pair))
        strmap_add(out, key, pair->value);
}

void
header_copy_list(const struct strmap *in, struct strmap *out,
                 const char *const*keys)
{
    assert(in != nullptr);
    assert(out != nullptr);
    assert(keys != nullptr);

    for (; *keys != nullptr; ++keys)
        header_copy_one(in, out, *keys);
}

void
header_copy_prefix(struct strmap *in, struct strmap *out, const char *prefix)
{
    assert(in != nullptr);
    assert(out != nullptr);
    assert(prefix != nullptr);
    assert(*prefix != 0);

    strmap_rewind(in);

    const size_t prefix_length = strlen(prefix);

    const struct strmap_pair *pair;
    while ((pair = strmap_next(in)) != nullptr)
        if (memcmp(pair->key, prefix, prefix_length) == 0)
            strmap_add(out, pair->key, pair->value);
}
