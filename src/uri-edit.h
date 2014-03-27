/*
 * Functions for editing URIs.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_URI_EDIT_H
#define BENG_URI_EDIT_H

#include <inline/compiler.h>

#include <stddef.h>

struct pool;

#ifdef __cplusplus
extern "C" {
#endif

gcc_pure gcc_malloc
const char *
uri_insert_query_string(struct pool *pool, const char *uri,
                        const char *query_string);

/**
 * Appends the specified query string at the end.  Adds a '?' or '&'
 * if appropriate.
 */
gcc_pure gcc_malloc
const char *
uri_append_query_string_n(struct pool *pool, const char *uri,
                          const char *query_string, size_t length);

gcc_pure gcc_malloc
const char *
uri_delete_query_string(struct pool *pool, const char *uri,
                        const char *needle, size_t needle_length);

gcc_pure gcc_malloc
const char *
uri_insert_args(struct pool *pool, const char *uri,
                const char *args, size_t args_length,
                const char *path, size_t path_length);

#ifdef __cplusplus
}
#endif

#endif
