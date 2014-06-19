/*
 * Container for cookies received from other HTTP servers.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_COOKIE_JAR_HXX
#define BENG_PROXY_COOKIE_JAR_HXX

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

    gcc_malloc
    struct cookie *Dup(struct dpool &pool) const;

    void Free(struct dpool &pool);
};

struct cookie_jar {
    struct dpool &pool;

    struct list_head cookies;

    cookie_jar(struct dpool &_pool)
        :pool(_pool) {
        list_init(&cookies);
    }

    gcc_malloc
    struct cookie_jar *Dup(struct dpool &new_pool) const;

    void Free();

    void Add(struct cookie &cookie) {
        list_add(&cookie.siblings, &cookies);
    }

    void EraseAndDispose(struct cookie &cookie);
};

struct cookie_jar * gcc_malloc
cookie_jar_new(struct dpool &pool);

#endif
