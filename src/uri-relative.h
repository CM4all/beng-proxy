/*
 * Functions for working with relative URIs.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef __BENG_URI_H
#define __BENG_URI_H

#include "pool.h"

/**
 * Append a relative URI to an absolute base URI, and return the
 * resulting absolute URI.  Returns NULL if the provided relative URI
 * does not match the base URI.
 */
const char *
uri_absolute(pool_t pool, const char *base, const char *uri, size_t length);

#endif
