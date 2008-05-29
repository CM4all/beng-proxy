#include "tpool.h"
#include "header-parser.h"
#include "growing-buffer.h"
#include "strmap.h"

#include <unistd.h>
#include <stdio.h>

int main(int argc __attr_unused, char **argv __attr_unused) {
    pool_t pool;
    struct growing_buffer *gb;
    char buffer[16];
    ssize_t nbytes;
    struct strmap *headers;
    const struct strmap_pair *pair;

    pool = pool_new_libc(NULL, "root");
    tpool_init(pool);

    gb = growing_buffer_new(pool, sizeof(buffer));

    /* read input from stdin */

    while ((nbytes = read(0, buffer, sizeof(buffer))) > 0)
        growing_buffer_write_buffer(gb, buffer, (size_t)nbytes);

    /* parse the headers */

    headers = strmap_new(pool, 16);
    header_parse_buffer(pool, headers, gb);

    /* dump headers */

    strmap_rewind(headers);
    while ((pair = strmap_next(headers)) != NULL)
        printf("%s: %s\n", pair->key, pair->value);

    /* cleanup */

    tpool_deinit();
    pool_unref(pool);
    pool_commit();
    pool_recycler_clear();
}
