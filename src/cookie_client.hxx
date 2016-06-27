/*
 * Manage cookies sent by the widget server.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_COOKIE_CLIENT_HXX
#define BENG_PROXY_COOKIE_CLIENT_HXX

struct pool;
struct StringMap;
struct CookieJar;

/**
 * Parse a Set-Cookie2 response header and insert new cookies into the
 * linked list.
 *
 * @param path the URI path, used for verification; if nullptr, all
 * cookie paths are accepted
 */
void
cookie_jar_set_cookie2(CookieJar *jar, const char *value,
                       const char *domain, const char *path);

/**
 * Generate the HTTP request header for cookies in the jar.
 */
char *
cookie_jar_http_header_value(const CookieJar *jar,
                             const char *domain, const char *path,
                             struct pool *pool);

/**
 * Generate HTTP request headers passing for all cookies in the linked
 * list.
 */
void
cookie_jar_http_header(const CookieJar *jar,
                       const char *domain, const char *path,
                       StringMap *headers, struct pool *pool);

#endif
