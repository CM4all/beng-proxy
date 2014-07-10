#include "cookie_server.hxx"
#include "strmap.hxx"
#include "pool.h"

#include <stdio.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>

int main(int argc, char **argv) {
    struct pool *pool;
    struct strmap *cookies;

    pool = pool_new_libc(nullptr, "root");

    cookies = strmap_new(pool);
    for (int i = 1; i < argc; ++i)
        cookie_map_parse(cookies, argv[i], pool);

    for (const auto &i : *cookies)
        printf("%s=%s\n", i.key, i.value);

    pool_unref(pool);
    pool_commit();
    pool_recycler_clear();
}
