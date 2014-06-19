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
cookie::Free(struct dpool &pool)
{
    if (!strref_is_empty(&name))
        strref_free_d(&pool, &name);

    if (!strref_is_empty(&value))
        strref_free_d(&pool, &value);

    if (domain != nullptr)
        d_free(&pool, domain);

    if (path != nullptr)
        d_free(&pool, path);

    d_free(&pool, this);
}

void
cookie_jar::EraseAndDispose(struct cookie &cookie)
{
    assert(&cookie.siblings != &cookies);

    list_remove(&cookie.siblings);

    cookie.Free(pool);
}

struct cookie_jar *
cookie_jar_new(struct dpool &pool)
{
    return NewFromPool<struct cookie_jar>(&pool, pool);
}

void
cookie_jar::Free()
{
    while (!list_empty(&cookies)) {
        struct cookie *cookie = (struct cookie *)cookies.next;

        list_remove(&cookie->siblings);
        cookie->Free(pool);
    }

    d_free(&pool, this);
}

struct cookie *
cookie::Dup(struct dpool &pool) const
{
    assert(domain != nullptr);

    auto dest = NewFromPool<struct cookie>(&pool);
    if (dest == nullptr)
        return nullptr;

    strref_set_dup_d(&pool, &dest->name, &name);
    strref_set_dup_d(&pool, &dest->value, &value);

    dest->domain = d_strdup(&pool, domain);
    if (dest->domain == nullptr)
        return nullptr;

    if (path != nullptr) {
        dest->path = d_strdup(&pool, path);
        if (dest->path == nullptr)
            return nullptr;
    } else
        dest->path = nullptr;

    dest->expires = expires;

    return dest;
}

struct cookie_jar * gcc_malloc
cookie_jar::Dup(struct dpool &new_pool) const
{
    auto dest = NewFromPool<struct cookie_jar>(&new_pool, new_pool);
    if (dest == nullptr)
        return nullptr;

    struct cookie *src_cookie;
    for (src_cookie = (struct cookie *)cookies.next;
         &src_cookie->siblings != &cookies;
         src_cookie = (struct cookie *)src_cookie->siblings.next) {
        struct cookie *dest_cookie = src_cookie->Dup(new_pool);
        if (dest_cookie == nullptr) {
            dest->Free();
            return nullptr;
        }

        dest->Add(*dest_cookie);
    }

    return dest;
}
