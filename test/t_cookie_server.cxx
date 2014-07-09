#include "cookie_server.hxx"
#include "header_writer.hxx"
#include "strmap.hxx"
#include "pool.h"

#include <assert.h>
#include <string.h>
#include <stdio.h>

int main(int argc gcc_unused, char **argv gcc_unused) {
    struct pool *pool;

    pool = pool_new_libc(nullptr, "root");

    struct strmap *cookies = strmap_new(pool);

    cookie_map_parse(cookies, "a=b", pool);
    assert(strcmp(strmap_get(cookies, "a"), "b") == 0);

    cookie_map_parse(cookies, "c=d;e=f", pool);
    assert(strcmp(strmap_get(cookies, "c"), "d") == 0);
    assert(strcmp(strmap_get(cookies, "e"), "f") == 0);

    cookie_map_parse(cookies, "quoted=\"quoted!\\\\", pool);
    assert(strcmp(strmap_get(cookies, "quoted"), "quoted!\\") == 0);

    cookie_map_parse(cookies, "invalid1=foo\t", pool);
    assert(strcmp(strmap_get(cookies, "invalid1"), "foo") == 0);

    /* this is actually invalid, but unfortunately RFC ignorance is
       viral, and forces us to accept square brackets :-( */
    cookie_map_parse(cookies, "invalid2=foo |[bar] ,", pool);
    assert(strcmp(strmap_get(cookies, "invalid2"), "foo |[bar] ,") == 0);

    assert(strcmp(cookie_exclude("foo=\"bar\"", "abc", pool),
                  "foo=\"bar\"") == 0);

    assert(cookie_exclude("foo=\"bar\"", "foo", pool) == nullptr);

    assert(strcmp(cookie_exclude("a=\"b\"", "foo", pool),
                  "a=\"b\"") == 0);

    assert(strcmp(cookie_exclude("a=b", "foo", pool),
                  "a=b") == 0);

    assert(strcmp(cookie_exclude("a=\"b\"; foo=\"bar\"; c=\"d\"", "foo", pool),
                  "a=\"b\"; c=\"d\"") == 0);

    assert(strcmp(cookie_exclude("foo=\"bar\"; c=\"d\"", "foo", pool),
                  "c=\"d\"") == 0);

    assert(strcmp(cookie_exclude("a=\"b\"; foo=\"bar\"", "foo", pool),
                  "a=\"b\"; ") == 0);

    assert(strcmp(cookie_exclude("foo=\"duplicate\"; a=\"b\"; foo=\"bar\"; c=\"d\"", "foo", pool),
                  "a=\"b\"; c=\"d\"") == 0);

    pool_unref(pool);
    pool_commit();
    pool_recycler_clear();
}
