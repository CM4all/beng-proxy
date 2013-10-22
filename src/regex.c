/*
 * Utilities for dealing with regular expressions.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "regex.h"
#include "pool.h"
#include "uri-escape.h"

#include <glib.h>

#include <assert.h>
#include <string.h>

gcc_const
static inline GQuark
expand_quark(void)
{
    return g_quark_from_static_string("expand");
}

const char *
expand_string(struct pool *pool, const char *src,
              const GMatchInfo *match_info, GError **error_r)
{
    assert(pool != NULL);
    assert(src != NULL);
    assert(match_info != NULL);

    char *p = g_match_info_expand_references(match_info, src, error_r);
    if (p == NULL)
        return NULL;

    /* move result to the memory pool */
    char *q = p_strdup(pool, p);
    g_free(p);
    return q;
}

const char *
expand_string_unescaped(struct pool *pool, const char *src,
                        const GMatchInfo *match_info,
                        GError **error_r)
{
    assert(pool != NULL);
    assert(src != NULL);
    assert(match_info != NULL);

    GString *result = g_string_sized_new(256);

    while (true) {
        const char *backslash = strchr(src, '\\');
        if (backslash == NULL) {
            /* append the remaining input string and return */
            g_string_append(result, src);
            const char *result2 = p_strndup(pool, result->str, result->len);
            g_string_free(result, true);
            return result2;
        }

        /* copy everything up to the backslash */
        g_string_append_len(result, src, backslash - src);

        /* now evaluate the escape */
        src = backslash + 1;
        const char ch = *src++;
        if (ch == '\\')
            g_string_append_c(result, ch);
        else if (ch >= '0' && ch <= '9') {
            char *s = g_match_info_fetch(match_info, ch - '0');
            if (s != NULL) {
                const size_t length = uri_unescape_inplace(s, strlen(s), '%');
                g_string_append_len(result, s, length);
                g_free(s);
            }
        } else {
            g_set_error(error_r, expand_quark(), 0,
                        "Invalid backslash escape (0x%02x)", ch);
        }
    }
}
