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

#ifndef BENG_PROXY_COOKIE_JAR_HXX
#define BENG_PROXY_COOKIE_JAR_HXX

#include "util/Expiry.hxx"
#include "util/StringView.hxx"

#include <boost/intrusive/list.hpp>

struct pool;
struct dpool;
struct CookieJar;

struct Cookie
    : boost::intrusive::list_base_hook<boost::intrusive::link_mode<boost::intrusive::normal_link>> {
    StringView name;
    StringView value;
    const char *domain = nullptr, *path = nullptr;
    Expiry expires = Expiry::Never();

    struct Disposer {
        struct dpool &pool;

        explicit Disposer(struct dpool &_pool):pool(_pool) {}

        void operator()(Cookie *cookie) const {
            cookie->Free(pool);
        }
    };

    Cookie(struct dpool &pool, StringView _name, StringView _value);
    Cookie(struct dpool &pool, const Cookie &src);

    Cookie(const Cookie &) = delete;
    Cookie &operator=(const Cookie &) = delete;

    void Free(struct dpool &pool);
};

/**
 * Container for cookies received from other HTTP servers.
 */
struct CookieJar {
    struct dpool &pool;

    typedef boost::intrusive::list<Cookie,
                                   boost::intrusive::constant_time_size<false>> List;
    List cookies;

    explicit CookieJar(struct dpool &_pool)
        :pool(_pool) {
    }

    CookieJar(struct dpool &_pool, const CookieJar &src);

    CookieJar(const CookieJar &) = delete;
    CookieJar &operator=(const CookieJar &) = delete;

    void Free();

    void Add(Cookie &cookie) {
        cookies.push_front(cookie);
    }

    void EraseAndDispose(Cookie &cookie);

    /**
     * Delete expired cookies.
     */
    void Expire(Expiry now);
};

#endif
