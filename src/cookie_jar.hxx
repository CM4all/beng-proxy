/*
 * Container for cookies received from other HTTP servers.
 *
 * author: Max Kellermann <mk@cm4all.com>
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
