#include "cookie_server.hxx"
#include "strmap.hxx"
#include "RootPool.hxx"

#include <stdio.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>

int main(int argc, char **argv) {
    RootPool pool;

    auto *cookies = strmap_new(pool);
    for (int i = 1; i < argc; ++i)
        cookie_map_parse(cookies, argv[i], pool);

    for (const auto &i : *cookies)
        printf("%s=%s\n", i.key, i.value);
}
