/*
 * Container for cookies received from other HTTP servers.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_COOKIE_JAR_HXX
#define BENG_PROXY_COOKIE_JAR_HXX

#include "util/StringView.hxx"

#include <inline/compiler.h>

#include <boost/intrusive/list.hpp>

#include <new>

#include <sys/types.h>

struct pool;
struct dpool;
struct CookieJar;

struct Cookie
    : boost::intrusive::list_base_hook<boost::intrusive::link_mode<boost::intrusive::normal_link>> {
    StringView name;
    StringView value;
    const char *domain = nullptr, *path = nullptr;
    time_t expires = 0;

    struct Disposer {
        struct dpool &pool;

        explicit Disposer(struct dpool &_pool):pool(_pool) {}

        void operator()(Cookie *cookie) const {
            cookie->Free(pool);
        }
    };

    gcc_malloc
    Cookie *Dup(struct dpool &pool) const
        throw(std::bad_alloc);

    void Free(struct dpool &pool);
};

struct CookieJar {
    struct dpool &pool;

    typedef boost::intrusive::list<Cookie,
                                   boost::intrusive::constant_time_size<false>> List;
    List cookies;

    CookieJar(struct dpool &_pool)
        :pool(_pool) {
    }

    gcc_malloc
    CookieJar *Dup(struct dpool &new_pool) const;

    void Free();

    void Add(Cookie &cookie) {
        cookies.push_front(cookie);
    }

    void EraseAndDispose(Cookie &cookie);
};

CookieJar *
cookie_jar_new(struct dpool &pool)
    throw(std::bad_alloc);

#endif
