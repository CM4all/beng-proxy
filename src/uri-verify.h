/*
 * Verify URI parts.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_URI_VERIFY_H
#define BENG_PROXY_URI_VERIFY_H

#include <inline/compiler.h>

#include <stdbool.h>
#include <stddef.h>

/**
 * Verifies one path segment of an URI according to RFC 2396.
 */
gcc_pure
bool
uri_segment_verify(const char *src, const char *end);

/**
 * Verifies the path portion of an URI according to RFC 2396.
 */
gcc_pure
bool
uri_path_verify(const char *src, size_t length);

/**
 * Performs some paranoid checks on the URI; the following is not
 * allowed:
 *
 * - %00
 * - %2f (encoded slash)
 * - "/../", "/./"
 * - "/..", "/." at the end
 *
 * It is assumed that the URI was already verified with
 * uri_path_verify().
 */
gcc_pure
bool
uri_path_verify_paranoid(const char *uri);

/**
 * Quickly verify the validity of an URI (path plus query).  This may
 * be used before passing it to another server, not to be parsed by
 * this process.
 */
gcc_pure
bool
uri_verify_quick(const char *uri);

#endif
