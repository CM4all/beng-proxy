/*
 * Container for cookies received from other HTTP servers.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "cookie_jar.hxx"
#include "shm/dpool.hxx"

#include <string.h>

void
Cookie::Free(struct dpool &pool)
{
    if (!name.IsEmpty())
        d_free(&pool, name.data);

    if (!value.IsEmpty())
        d_free(&pool, value.data);

    if (domain != nullptr)
        d_free(&pool, domain);

    if (path != nullptr)
        d_free(&pool, path);

    d_free(&pool, this);
}

void
CookieJar::EraseAndDispose(Cookie &cookie)
{
    cookies.erase_and_dispose(cookies.iterator_to(cookie),
                              Cookie::Disposer(pool));
}

CookieJar *
cookie_jar_new(struct dpool &pool)
{
    return NewFromPool<CookieJar>(&pool, pool);
}

void
CookieJar::Free()
{
    cookies.clear_and_dispose(Cookie::Disposer(pool));

    d_free(&pool, this);
}

Cookie *
Cookie::Dup(struct dpool &pool) const
{
    assert(domain != nullptr);

    auto dest = NewFromPool<Cookie>(&pool);
    if (dest == nullptr)
        return nullptr;

    dest->name = DupStringView(pool, name);
    dest->value = DupStringView(pool, value);

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

CookieJar * gcc_malloc
CookieJar::Dup(struct dpool &new_pool) const
{
    auto dest = NewFromPool<CookieJar>(&new_pool, new_pool);
    if (dest == nullptr)
        return nullptr;

    for (const auto &src_cookie : cookies) {
        auto *dest_cookie = src_cookie.Dup(new_pool);
        if (dest_cookie == nullptr) {
            dest->Free();
            return nullptr;
        }

        dest->Add(*dest_cookie);
    }

    return dest;
}
