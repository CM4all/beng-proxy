/*
 * Container for cookies received from other HTTP servers.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_COOKIE_JAR_HXX
#define BENG_PROXY_COOKIE_JAR_HXX

#include "strref.h"

#include <inline/compiler.h>

#include <boost/intrusive/list.hpp>

#include <sys/types.h>

struct pool;
struct dpool;
struct cookie_jar;

struct cookie
    : boost::intrusive::list_base_hook<boost::intrusive::link_mode<boost::intrusive::normal_link>> {
    struct strref name;
    struct strref value;
    const char *domain, *path;
    time_t expires;

    struct Disposer {
        struct dpool &pool;

        explicit Disposer(struct dpool &_pool):pool(_pool) {}

        void operator()(struct cookie *cookie) const {
            cookie->Free(pool);
        }
    };

    gcc_malloc
    struct cookie *Dup(struct dpool &pool) const;

    void Free(struct dpool &pool);
};

struct cookie_jar {
    struct dpool &pool;

    typedef boost::intrusive::list<struct cookie,
                                   boost::intrusive::constant_time_size<false>> List;
    List cookies;

    cookie_jar(struct dpool &_pool)
        :pool(_pool) {
    }

    gcc_malloc
    struct cookie_jar *Dup(struct dpool &new_pool) const;

    void Free();

    void Add(struct cookie &cookie) {
        cookies.push_front(cookie);
    }

    void EraseAndDispose(struct cookie &cookie);
};

struct cookie_jar * gcc_malloc
cookie_jar_new(struct dpool &pool);

#endif
