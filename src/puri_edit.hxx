/*
 * Functions for editing URIs.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_URI_EDIT_HXX
#define BENG_URI_EDIT_HXX

#include <inline/compiler.h>

#include <stddef.h>

struct pool;
struct StringView;

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
                          StringView query_string);

gcc_pure gcc_malloc
const char *
uri_delete_query_string(struct pool *pool, const char *uri,
                        StringView needle);

gcc_pure gcc_malloc
const char *
uri_insert_args(struct pool *pool, const char *uri,
                StringView args, StringView path);

#endif
