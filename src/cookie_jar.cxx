/*
 * Container for cookies received from other HTTP servers.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "cookie_jar.hxx"
#include "shm/dpool.hxx"

#include <string.h>

void
cookie::Free(struct dpool &pool)
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
cookie_jar::EraseAndDispose(struct cookie &cookie)
{
    cookies.erase_and_dispose(cookies.iterator_to(cookie),
                              cookie::Disposer(pool));
}

struct cookie_jar *
cookie_jar_new(struct dpool &pool)
{
    return NewFromPool<struct cookie_jar>(&pool, pool);
}

void
cookie_jar::Free()
{
    cookies.clear_and_dispose(cookie::Disposer(pool));

    d_free(&pool, this);
}

struct cookie *
cookie::Dup(struct dpool &pool) const
{
    assert(domain != nullptr);

    auto dest = NewFromPool<struct cookie>(&pool);
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

struct cookie_jar * gcc_malloc
cookie_jar::Dup(struct dpool &new_pool) const
{
    auto dest = NewFromPool<struct cookie_jar>(&new_pool, new_pool);
    if (dest == nullptr)
        return nullptr;

    for (const auto &src_cookie : cookies) {
        struct cookie *dest_cookie = src_cookie.Dup(new_pool);
        if (dest_cookie == nullptr) {
            dest->Free();
            return nullptr;
        }

        dest->Add(*dest_cookie);
    }

    return dest;
}
