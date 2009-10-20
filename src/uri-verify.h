/*
 * Verify URI parts.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_URI_VERIFY_H
#define BENG_PROXY_URI_VERIFY_H

#include <stdbool.h>
#include <stddef.h>

/**
 * Verifies one path segment of an URI according to RFC 2396.
 */
bool
uri_segment_verify(const char *src, const char *end);

/**
 * Verifies the path portion of an URI according to RFC 2396.
 */
bool
uri_path_verify(const char *src, size_t length);

#endif
