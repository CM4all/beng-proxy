/*
 * Container for cookies received from other HTTP servers.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "cookie_jar.hxx"
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

    if (cookie->domain != nullptr)
        d_free(pool, cookie->domain);

    if (cookie->path != nullptr)
        d_free(pool, cookie->path);

    d_free(pool, cookie);
}

void
cookie_delete(struct cookie_jar *jar, struct cookie *cookie)
{
    assert(jar != nullptr);
    assert(cookie != nullptr);
    assert(&cookie->siblings != &jar->cookies);

    list_remove(&cookie->siblings);

    cookie_free(jar->pool, cookie);
}

struct cookie_jar *
cookie_jar_new(struct dpool *pool)
{
    struct cookie_jar *jar = (struct cookie_jar *)d_malloc(pool, sizeof(*jar));
    if (jar == nullptr)
        return nullptr;

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
    assert(src != nullptr);
    assert(src->domain != nullptr);

    struct cookie *dest = (struct cookie *)d_malloc(pool, sizeof(*dest));
    if (dest == nullptr)
        return nullptr;

    strref_set_dup_d(pool, &dest->name, &src->name);
    strref_set_dup_d(pool, &dest->value, &src->value);

    dest->domain = d_strdup(pool, src->domain);
    if (dest->domain == nullptr)
        return nullptr;

    if (src->path != nullptr) {
        dest->path = d_strdup(pool, src->path);
        if (dest->path == nullptr)
            return nullptr;
    } else
        dest->path = nullptr;

    dest->expires = src->expires;

    return dest;
}

struct cookie_jar * gcc_malloc
cookie_jar_dup(struct dpool *pool, const struct cookie_jar *src)
{
    struct cookie_jar *dest = (struct cookie_jar *)
        d_malloc(pool, sizeof(*dest));
    if (dest == nullptr)
        return nullptr;

    dest->pool = pool;
    list_init(&dest->cookies);

    struct cookie *src_cookie;
    for (src_cookie = (struct cookie *)src->cookies.next;
         &src_cookie->siblings != &src->cookies;
         src_cookie = (struct cookie *)src_cookie->siblings.next) {
        struct cookie *dest_cookie = cookie_dup(pool, src_cookie);
        if (dest_cookie == nullptr) {
            cookie_jar_free(dest);
            return nullptr;
        }

        list_add(&dest_cookie->siblings, &dest->cookies);
    }

    return dest;
}
