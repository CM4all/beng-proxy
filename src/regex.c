/*
 * Utilities for dealing with regular expressions.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "regex.h"
#include "pool.h"

#include <assert.h>

const char *
expand_string(struct pool *pool, const char *src, const GMatchInfo *match_info)
{
    assert(pool != NULL);
    assert(src != NULL);
    assert(match_info != NULL);

    char *p = g_match_info_expand_references(match_info, src, NULL);
    if (p == NULL)
        /* XXX an error has occurred; how to report to the caller? */
        return src;

    /* move result to the memory pool */
    char *q = p_strdup(pool, p);
    g_free(p);
    return q;
}
