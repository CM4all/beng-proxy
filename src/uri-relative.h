/*
 * Functions for working with relative URIs.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef __BENG_URI_H
#define __BENG_URI_H

#include "pool.h"

struct strref;

/**
 * Compresses an URI (eliminates all "/./" and "/../"), and returns
 * the result.  May return NULL if there are too many "/../".
 */
const char *
uri_compress(pool_t pool, const char *uri);

/**
 * Append a relative URI to an absolute base URI, and return the
 * resulting absolute URI.
 */
const char *
uri_absolute(pool_t pool, const char *base, const char *uri, size_t length);

/**
 * Check if an (absolute) URI is relative to an a base URI (also
 * absolute), and return the relative part.  Returns NULL if both URIs
 * do not match.
 */
const struct strref *
uri_relative(const struct strref *base, struct strref *uri);

#endif
