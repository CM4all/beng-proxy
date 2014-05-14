#include "cookie_server.hxx"
#include "header-writer.h"
#include "strmap.h"
#include "pool.h"

#include <assert.h>
#include <string.h>
#include <stdio.h>

int main(int argc gcc_unused, char **argv gcc_unused) {
    struct pool *pool;
    struct strmap *cookies;

    pool = pool_new_libc(nullptr, "root");

    cookies = strmap_new(pool, 17);

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

    pool_unref(pool);
    pool_commit();
    pool_recycler_clear();
}
