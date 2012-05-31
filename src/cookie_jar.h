/*
 * Container for cookies received from other HTTP servers.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_COOKIE_JAR_H
#define BENG_PROXY_COOKIE_JAR_H

#include "strref.h"

#include <inline/compiler.h>
#include <inline/list.h>

#include <sys/types.h>

struct pool;
struct dpool;
struct cookie_jar;

struct cookie {
    struct list_head siblings;

    struct strref name;
    struct strref value;
    const char *domain, *path;
    time_t expires;
};

struct cookie_jar {
    struct dpool *pool;

    struct list_head cookies;
};

void
cookie_free(struct dpool *pool, struct cookie *cookie);

struct cookie_jar * gcc_malloc
cookie_jar_new(struct dpool *pool);

void
cookie_jar_free(struct cookie_jar *jar);

struct cookie_jar * gcc_malloc
cookie_jar_dup(struct dpool *pool, const struct cookie_jar *src);

static inline void
cookie_jar_add(struct cookie_jar *jar, struct cookie *cookie)
{
    list_add(&cookie->siblings, &jar->cookies);
}

void
cookie_delete(struct cookie_jar *jar, struct cookie *cookie);

#endif
