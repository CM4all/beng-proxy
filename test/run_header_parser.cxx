#include "RootPool.hxx"
#include "header_parser.hxx"
#include "GrowingBuffer.hxx"
#include "strmap.hxx"

#include <unistd.h>
#include <stdio.h>

int main(int argc gcc_unused, char **argv gcc_unused) {
    char buffer[16];
    ssize_t nbytes;

    RootPool pool;

    GrowingBuffer gb(pool, sizeof(buffer));

    /* read input from stdin */

    while ((nbytes = read(0, buffer, sizeof(buffer))) > 0)
        gb.Write(buffer, (size_t)nbytes);

    /* parse the headers */

    auto *headers = strmap_new(pool);
    header_parse_buffer(pool, *headers, gb);

    /* dump headers */

    for (const auto &i : *headers)
        printf("%s: %s\n", i.key, i.value);
}
