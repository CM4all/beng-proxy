/*
 * Functions for editing URIs.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "puri_edit.hxx"
#include "pool.hxx"
#include "util/StringView.hxx"

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
                          StringView query_string)
{
    assert(pool != nullptr);
    assert(uri != nullptr);
    assert(!query_string.IsNull());
    assert(!query_string.IsEmpty());

    return p_strncat(pool, uri, strlen(uri),
                     strchr(uri, '?') == nullptr ? "?" : "&", (size_t)1,
                     query_string.data, query_string.size,
                     nullptr);
}

static size_t
query_string_begins_with(const char *query_string, StringView needle)
{
    assert(query_string != nullptr);
    assert(!needle.IsNull());

    if (memcmp(query_string, needle.data, needle.size) != 0)
        return 0;

    query_string += needle.size;
    if (*query_string == '&')
        return needle.size + 1;
    else if (*query_string == 0)
        return needle.size;
    else
        return 0;
}

const char *
uri_delete_query_string(struct pool *pool, const char *uri,
                        StringView needle)
{
    assert(pool != nullptr);
    assert(uri != nullptr);
    assert(!needle.IsNull());

    const char *p = strchr(uri, '?');
    if (p == nullptr)
        /* no query string, nothing to remove */
        return uri;

    ++p;
    size_t delete_length = query_string_begins_with(p, needle);
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
                StringView args, StringView path)
{
    const char *q = strchr(uri, '?');
    if (q == nullptr)
        q = uri + strlen(uri);

    return p_strncat(pool, uri, q - uri,
                     ";", (size_t)1, args.data, args.size,
                     path.data, path.size,
                     q, strlen(q),
                     nullptr);
}
