/*
 * Manage cookies sent by the widget server.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef __BENG_COOKIE_CLIENT_H
#define __BENG_COOKIE_CLIENT_H

#include "pool.h"

struct dpool;
struct strmap;
struct cookie_jar;

struct cookie_jar * __attr_malloc
cookie_jar_new(struct dpool *pool);

void
cookie_jar_free(struct cookie_jar *jar);

struct cookie_jar * __attr_malloc
cookie_jar_dup(struct dpool *pool, const struct cookie_jar *src);

/**
 * Parse a Set-Cookie2 response header and insert new cookies into the
 * linked list.
 *
 * @param path the URI path, used for verification; if NULL, all
 * cookie paths are accepted
 */
void
cookie_jar_set_cookie2(struct cookie_jar *jar, const char *value,
                       const char *domain, const char *path);

/**
 * Generate the HTTP request header for cookies in the jar.
 */
char *
cookie_jar_http_header_value(struct cookie_jar *jar,
                             const char *domain, const char *path,
                             pool_t pool);

/**
 * Generate HTTP request headers passing for all cookies in the linked
 * list.
 */
void
cookie_jar_http_header(struct cookie_jar *jar,
                       const char *domain, const char *path,
                       struct strmap *headers, pool_t pool);

#endif
