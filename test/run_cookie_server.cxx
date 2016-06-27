#include "cookie_server.hxx"
#include "strmap.hxx"
#include "RootPool.hxx"

#include <stdio.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>

int main(int argc, char **argv) {
    RootPool pool;

    StringMap cookies(pool);
    for (int i = 1; i < argc; ++i)
        cookies.Merge(cookie_map_parse(pool, argv[i]));

    for (const auto &i : cookies)
        printf("%s=%s\n", i.key, i.value);
}
