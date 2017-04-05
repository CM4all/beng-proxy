/*
 * Various utilities for working with HTTP objects.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_HTTP_UTIL_HXX
#define BENG_PROXY_HTTP_UTIL_HXX

#include "strmap.hxx"
#include "http/List.hxx"

struct pool;
struct StringView;

/**
 * Splits a comma separated list into a string array.  The return
 * value is nullptr terminated.
 */
char **
http_list_split(struct pool &pool, const char *p);

gcc_pure
static inline int
http_client_accepts_encoding(const StringMap &request_headers,
                             const char *coding)
{
    const char *accept_encoding = request_headers.Get("accept-encoding");
    return accept_encoding != nullptr &&
        http_list_contains(accept_encoding, coding);
}

gcc_pure
StringView
http_header_param(const char *value, const char *name);

#endif
