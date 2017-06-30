/*
 * Escape and unescape in URI style ('%20').
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_URI_ESCAPE_HXX
#define BENG_PROXY_URI_ESCAPE_HXX

#include "util/Compiler.h"

#include <stddef.h>

template<typename T> struct ConstBuffer;
struct StringView;

/**
 * @param escape_char the character that is used to escape; use '%'
 * for normal URIs
 */
size_t
uri_escape(char *dest, StringView src,
           char escape_char='%');

size_t
uri_escape(char *dest, ConstBuffer<void> src,
           char escape_char='%');

/**
 * @param escape_char the character that is used to escape; use '%'
 * for normal URIs
 * @return pointer to the end of the destination buffer (not
 * null-terminated) or nullptr on error
 */
char *
uri_unescape(char *dest, StringView src,
             char escape_char='%');

#endif
