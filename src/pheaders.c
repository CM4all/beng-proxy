/*
 * Process CM4all commands in a HTML stream, e.g. embeddings.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "pheaders.h"
#include "strmap.h"

static void
headers_copy2(struct strmap *in, struct strmap *out,
              const char *const* keys)
{
    const char *value;

    for (; *keys != NULL; ++keys) {
        value = strmap_get(in, *keys);
        if (value != NULL)
            strmap_set(out, *keys, value);
    }
}

struct strmap *
processor_header_forward(struct pool *pool, struct strmap *headers)
{
    if (headers == NULL)
        return NULL;

    static const char *const copy_headers[] = {
        "content-language",
        "content-type",
        "content-disposition",
        "location",
        NULL,
    };

    struct strmap *headers2 = strmap_new(pool, 8);
    headers_copy2(headers, headers2, copy_headers);
    return headers2;
}
