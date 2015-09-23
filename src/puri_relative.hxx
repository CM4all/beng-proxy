/*
 * Functions for working with relative URIs.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PURI_RELATIVE_HXX
#define BENG_PURI_RELATIVE_HXX

#include <inline/compiler.h>

#include <stddef.h>

struct pool;

/**
 * Compresses an URI (eliminates all "/./" and "/../"), and returns
 * the result.  May return NULL if there are too many "/../".
 */
gcc_pure gcc_malloc
const char *
uri_compress(struct pool *pool, const char *uri);

/**
 * Append a relative URI to an absolute base URI, and return the
 * resulting absolute URI.  Will never return NULL, as there is no
 * error checking.
 */
gcc_pure gcc_malloc
const char *
uri_absolute(struct pool *pool, const char *base, const char *uri, size_t length);

#endif
