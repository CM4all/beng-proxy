#include "cookie-server.h"
#include "strmap.h"

#include <stdio.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>

int main(int argc, char **argv) {
    pool_t pool;
    struct strmap *cookies;
    const struct strmap_pair *pair;

    pool = pool_new_libc(NULL, "root");

    cookies = strmap_new(pool, 17);
    for (int i = 1; i < argc; ++i)
        cookie_map_parse(cookies, argv[i], pool);

    strmap_rewind(cookies);
    while ((pair = strmap_next(cookies)) != NULL)
        printf("%s=%s\n", pair->key, pair->value);

    pool_unref(pool);
    pool_commit();
    pool_recycler_clear();
}
