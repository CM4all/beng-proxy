/*
 * Functions for editing URIs.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_URI_EDIT_H
#define BENG_URI_EDIT_H

#include <stddef.h>

struct pool;

const char *
uri_insert_query_string(struct pool *pool, const char *uri,
                        const char *query_string);

/**
 * Appends the specified query string at the end.  Adds a '?' or '&'
 * if appropriate.
 */
const char *
uri_append_query_string_n(struct pool *pool, const char *uri,
                          const char *query_string, size_t length);

const char *
uri_delete_query_string(struct pool *pool, const char *uri,
                        const char *needle, size_t needle_length);

const char *
uri_insert_args(struct pool *pool, const char *uri,
                const char *args, size_t length);

#endif
