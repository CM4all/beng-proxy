/*
 * Functions for editing URIs.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "uri-edit.h"

#include <assert.h>
#include <string.h>

const char *
uri_insert_query_string(pool_t pool, const char *uri,
                        const char *query_string)
{
    const char *qmark = strchr(uri, '?');

    if (qmark != NULL) {
        ++qmark;
        return p_strncat(pool, uri, qmark - uri,
                         query_string, strlen(query_string),
                         "&", (size_t)1,
                         qmark, strlen(qmark),
                         NULL);
    } else
        return p_strcat(pool, uri, "?", query_string, NULL);
}

const char *
uri_append_query_string_n(pool_t pool, const char *uri,
                          const char *query_string, size_t length)
{
    assert(uri != NULL);
    assert(query_string != NULL);
    assert(length > 0);

    return p_strncat(pool, uri, strlen(uri),
                     strchr(uri, '?') == NULL ? "?" : "&", (size_t)1,
                     query_string, length,
                     NULL);
}

static size_t
query_string_begins_with(const char *query_string, const char *needle,
                         size_t needle_length)
{
    if (memcmp(query_string, needle, needle_length) != 0)
        return 0;

    query_string += needle_length;
    if (*query_string == '&')
        return needle_length + 1;
    else if (*query_string == 0)
        return needle_length;
    else
        return 0;
}

const char *
uri_delete_query_string(pool_t pool, const char *uri,
                        const char *needle, size_t needle_length)
{
    const char *p;
    size_t delete_length;

    p = strchr(uri, '?');
    if (p == NULL)
        /* no query string, nothing to remove */
        return uri;

    ++p;
    delete_length = query_string_begins_with(p, needle, needle_length);
    if (delete_length == 0)
        /* mismatch, return original URI */
        return uri;

    if (p[delete_length] == 0) {
        /* empty query string - also delete the question mark */
        --p;
        ++delete_length;
    }

    return p_strncat(pool, uri, p - uri,
                     p + delete_length, strlen(p + delete_length),
                     NULL);
}
