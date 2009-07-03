/*
 * Functions for editing URIs.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_URI_EDIT_H
#define BENG_URI_EDIT_H

#include "pool.h"

const char *
uri_insert_query_string(pool_t pool, const char *uri,
                        const char *query_string);

/**
 * Appends the specified query string at the end.  Adds a '?' or '&'
 * if appropriate.
 */
const char *
uri_append_query_string_n(pool_t pool, const char *uri,
                          const char *query_string, size_t length);

const char *
uri_delete_query_string(pool_t pool, const char *uri,
                        const char *needle, size_t needle_length);

bool
uri_forward_query_string(pool_t pool, const char *uri,
                         const char *needle, size_t needle_length);

#endif
