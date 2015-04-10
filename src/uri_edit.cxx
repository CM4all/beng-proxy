/*
 * Functions for editing URIs.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "uri_edit.hxx"
#include "pool.hxx"

#include <assert.h>
#include <string.h>

const char *
uri_insert_query_string(struct pool *pool, const char *uri,
                        const char *query_string)
{
    assert(pool != nullptr);
    assert(uri != nullptr);
    assert(query_string != nullptr);

    const char *qmark = strchr(uri, '?');

    if (qmark != nullptr) {
        ++qmark;
        return p_strncat(pool, uri, qmark - uri,
                         query_string, strlen(query_string),
                         "&", (size_t)1,
                         qmark, strlen(qmark),
                         nullptr);
    } else
        return p_strcat(pool, uri, "?", query_string, nullptr);
}

const char *
uri_append_query_string_n(struct pool *pool, const char *uri,
                          const char *query_string, size_t length)
{
    assert(pool != nullptr);
    assert(uri != nullptr);
    assert(query_string != nullptr);
    assert(length > 0);

    return p_strncat(pool, uri, strlen(uri),
                     strchr(uri, '?') == nullptr ? "?" : "&", (size_t)1,
                     query_string, length,
                     nullptr);
}

static size_t
query_string_begins_with(const char *query_string, const char *needle,
                         size_t needle_length)
{
    assert(query_string != nullptr);
    assert(needle != nullptr);

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
uri_delete_query_string(struct pool *pool, const char *uri,
                        const char *needle, size_t needle_length)
{
    assert(pool != nullptr);
    assert(uri != nullptr);
    assert(needle != nullptr);

    const char *p = strchr(uri, '?');
    if (p == nullptr)
        /* no query string, nothing to remove */
        return uri;

    ++p;
    size_t delete_length = query_string_begins_with(p, needle, needle_length);
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
                     nullptr);
}

const char *
uri_insert_args(struct pool *pool, const char *uri,
                const char *args, size_t args_length,
                const char *path, size_t path_length)
{
    const char *q = strchr(uri, '?');
    if (q == nullptr)
        q = uri + strlen(uri);

    return p_strncat(pool, uri, q - uri,
                     ";", (size_t)1, args, args_length,
                     path, path_length,
                     q, strlen(q),
                     nullptr);
}
