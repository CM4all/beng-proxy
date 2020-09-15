/*
 * Copyright 2007-2017 Content Management AG
 * All rights reserved.
 *
 * author: Max Kellermann <mk@cm4all.com>
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * - Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 *
 * - Redistributions in binary form must reproduce the above copyright
 * notice, this list of conditions and the following disclaimer in the
 * documentation and/or other materials provided with the
 * distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE
 * FOUNDATION OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED
 * OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "CookieJar.hxx"
#include "shm/dpool.hxx"

Cookie::Cookie(struct dpool &pool, StringView _name, StringView _value)
    :name(DupStringView(pool, _name)),
     value(DupStringView(pool, _value)) {}

Cookie::Cookie(struct dpool &pool, const Cookie &src)
    :name(DupStringView(pool, src.name)),
     value(DupStringView(pool, src.value)),
     domain(d_strdup(pool, src.domain)),
     path(d_strdup_checked(pool, src.path)),
     expires(src.expires) {}

void
Cookie::Free(struct dpool &pool)
{
    if (!name.empty())
        d_free(pool, name.data);

    if (!value.empty())
        d_free(pool, value.data);

    if (domain != nullptr)
        d_free(pool, domain);

    if (path != nullptr)
        d_free(pool, path);

    d_free(pool, this);
}

CookieJar::CookieJar(struct dpool &_pool, const CookieJar &src)
    :pool(_pool)
{
    for (const auto &src_cookie : src.cookies) {
        auto *dest_cookie = NewFromPool<Cookie>(pool, pool, src_cookie);
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

    d_free(pool, this);
}
