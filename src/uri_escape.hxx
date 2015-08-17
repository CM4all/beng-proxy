/*
 * Escape and unescape in URI style ('%20').
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_URI_ESCAPE_HXX
#define BENG_PROXY_URI_ESCAPE_HXX

#include <inline/compiler.h>

#include <stddef.h>

/**
 * @param escape_char the character that is used to escape; use '%'
 * for normal URIs
 */
size_t
uri_escape(char *dest, const char *src, size_t src_length,
           char escape_char='%');

/**
 * @param escape_char the character that is used to escape; use '%'
 * for normal URIs
 * @return pointer to the end of the destination buffer (not
 * null-terminated) or nullptr on error
 */
char *
uri_unescape(char *dest, const char *src, size_t length, char escape_char='%');

/**
 * @param escape_char the character that is used to escape; use '%'
 * for normal URIs
 */
size_t
uri_unescape_inplace(char *src, size_t length, char escape_char='%');

#endif
