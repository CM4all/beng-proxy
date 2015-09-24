/*
 * Escape and unescape in URI style ('%20').
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_PURI_ESCAPE_HXX
#define BENG_PROXY_PURI_ESCAPE_HXX

#include <inline/compiler.h>

struct pool;
struct StringView;

gcc_pure gcc_malloc
const char *
uri_escape_dup(struct pool *pool, StringView src,
               char escape_char='%');

/**
 * @return nullptr on error
 */
char *
uri_unescape_dup(struct pool *pool, StringView src,
                 char escape_char='%');

#endif
