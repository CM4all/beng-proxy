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

    const auto r = in->EqualRange(key);
    for (auto i = r.first; i != r.second; ++i)
        out->Add(key, i->value);
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
header_copy_prefix(const struct strmap *in, struct strmap *out,
                   const char *prefix)
{
    assert(in != nullptr);
    assert(out != nullptr);
    assert(prefix != nullptr);
    assert(*prefix != 0);

    const size_t prefix_length = strlen(prefix);

    for (const auto &i : *in)
        if (memcmp(i.key, prefix, prefix_length) == 0)
            out->Add(i.key, i.value);
}
