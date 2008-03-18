#include "cookie.h"
#include "header-writer.h"

#include <stdio.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>

int main(int argc, char **argv) {
    pool_t pool;
    int i;
    LIST_HEAD(cookies);
    struct strmap *headers;
    struct growing_buffer *gb;
    const void *data;
    size_t length;
    ssize_t nbytes;

    pool = pool_new_libc(NULL, "root");

    for (i = 1; i < argc; ++i)
        cookie_list_set_cookie2(pool, &cookies, argv[i]);

    headers = strmap_new(pool, 4);
    cookie_list_http_header(headers, &cookies, pool);

    gb = headers_dup(pool, headers);
    while ((data = growing_buffer_read(gb, &length)) != NULL) {
        nbytes = write(1, data, length);
        if (nbytes < 0) {
            perror("write() failed");
            return 1;
        }

        if (nbytes == 0)
            break;

        growing_buffer_consume(gb, (size_t)nbytes);
    }

    pool_unref(pool);
    pool_commit();
    pool_recycler_clear();
}
