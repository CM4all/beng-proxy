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

struct cookie_jar *
cookie_jar_new(struct dpool *pool);

void
cookie_jar_free(struct cookie_jar *jar);

/**
 * Parse a Set-Cookie2 response header and insert new cookies into the
 * linked list.
 */
void
cookie_jar_set_cookie2(struct cookie_jar *jar, const char *value,
                       const char *domain);

/**
 * Generate HTTP request headers passing for all cookies in the linked
 * list.
 */
void
cookie_jar_http_header(struct cookie_jar *jar,
                       const char *domain, const char *path,
                       struct strmap *headers, pool_t pool);

#endif
