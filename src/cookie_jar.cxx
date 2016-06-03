/*
 * Container for cookies received from other HTTP servers.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "cookie_jar.hxx"
#include "shm/dpool.hxx"

#include <string.h>

Cookie::Cookie(struct dpool &pool, StringView _name, StringView _value)
    :name(DupStringView(pool, _name)),
     value(DupStringView(pool, _value)) {}

Cookie::Cookie(struct dpool &pool, const Cookie &src)
    :name(DupStringView(pool, src.name)),
     value(DupStringView(pool, src.value)),
     domain(d_strdup(&pool, src.domain)),
     path(d_strdup_checked(&pool, src.path)),
     expires(src.expires) {}

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

CookieJar::CookieJar(struct dpool &_pool, const CookieJar &src)
    :pool(_pool)
{
    for (const auto &src_cookie : src.cookies) {
        auto *dest_cookie = NewFromPool<Cookie>(&pool, pool, src_cookie);
        Add(*dest_cookie);
    }
}

void
CookieJar::EraseAndDispose(Cookie &cookie)
{
    cookies.erase_and_dispose(cookies.iterator_to(cookie),
                              Cookie::Disposer(pool));
}

void
CookieJar::Expire(Expiry now)

{
    cookies.remove_and_dispose_if([now](const Cookie &cookie){
            return cookie.expires.IsExpired(now);
        }, Cookie::Disposer(pool));
}

void
CookieJar::Free()
{
    cookies.clear_and_dispose(Cookie::Disposer(pool));

    d_free(&pool, this);
}
