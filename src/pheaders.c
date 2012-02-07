/*
 * Process CM4all commands in a HTML stream, e.g. embeddings.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "pheaders.h"
#include "header-copy.h"
#include "strmap.h"

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
    header_copy_list(headers, headers2, copy_headers);

#ifndef NDEBUG
    /* copy Wildfire headers if present (debug build only, to avoid
       overhead on production servers) */
    if (strmap_get(headers, "x-wf-protocol-1") != NULL)
        header_copy_prefix(headers, headers2, "x-wf-");
#endif

    return headers2;
}
