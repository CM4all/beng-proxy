/*
 * Container for cookies received from other HTTP servers.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "cookie_jar.h"
#include "strref-dpool.h"
#include "dpool.h"

#include <string.h>

void
cookie_free(struct dpool *pool, struct cookie *cookie)
{
    if (!strref_is_empty(&cookie->name))
        strref_free_d(pool, &cookie->name);

    if (!strref_is_empty(&cookie->value))
        strref_free_d(pool, &cookie->value);

    if (cookie->domain != NULL)
        d_free(pool, cookie->domain);

    if (cookie->path != NULL)
        d_free(pool, cookie->path);

    d_free(pool, cookie);
}

void
cookie_delete(struct cookie_jar *jar, struct cookie *cookie)
{
    assert(jar != NULL);
    assert(cookie != NULL);
    assert(&cookie->siblings != &jar->cookies);

    list_remove(&cookie->siblings);

    cookie_free(jar->pool, cookie);
}

struct cookie_jar *
cookie_jar_new(struct dpool *pool)
{
    struct cookie_jar *jar = d_malloc(pool, sizeof(*jar));
    if (jar == NULL)
        return NULL;

    jar->pool = pool;
    list_init(&jar->cookies);
    return jar;
}

void
cookie_jar_free(struct cookie_jar *jar)
{
    while (!list_empty(&jar->cookies)) {
        struct cookie *cookie = (struct cookie *)jar->cookies.next;

        list_remove(&cookie->siblings);
        cookie_free(jar->pool, cookie);
    }

    d_free(jar->pool, jar);
}

static struct cookie * gcc_malloc
cookie_dup(struct dpool *pool, const struct cookie *src)
{
    struct cookie *dest;

    assert(src != NULL);
    assert(src->domain != NULL);

    dest = d_malloc(pool, sizeof(*dest));
    if (dest == NULL)
        return NULL;

    strref_set_dup_d(pool, &dest->name, &src->name);
    strref_set_dup_d(pool, &dest->value, &src->value);

    dest->domain = d_strdup(pool, src->domain);
    if (dest->domain == NULL)
        return NULL;

    if (src->path != NULL) {
        dest->path = d_strdup(pool, src->path);
        if (dest->path == NULL)
            return NULL;
    } else
        dest->path = NULL;

    dest->expires = src->expires;

    return dest;
}

struct cookie_jar * gcc_malloc
cookie_jar_dup(struct dpool *pool, const struct cookie_jar *src)
{
    struct cookie_jar *dest;
    struct cookie *src_cookie, *dest_cookie;

    dest = d_malloc(pool, sizeof(*dest));
    if (dest == NULL)
        return NULL;

    dest->pool = pool;
    list_init(&dest->cookies);

    for (src_cookie = (struct cookie *)src->cookies.next;
         &src_cookie->siblings != &src->cookies;
         src_cookie = (struct cookie *)src_cookie->siblings.next) {
        dest_cookie = cookie_dup(pool, src_cookie);
        if (dest_cookie == NULL) {
            cookie_jar_free(dest);
            return NULL;
        }

        list_add(&dest_cookie->siblings, &dest->cookies);
    }

    return dest;
}
