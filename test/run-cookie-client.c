#include "cookie-client.h"
#include "header-writer.h"
#include "tpool.h"
#include "shm.h"
#include "dpool.h"
#include "strmap.h"
#include "growing-buffer.h"

#include <stdio.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>

int main(int argc, char **argv) {
    pool_t pool = pool_new_libc(NULL, "root");
    tpool_init(pool);

    struct shm *shm = shm_new(1024, 512);
    struct dpool *dpool = dpool_new(shm);

    struct cookie_jar *jar = cookie_jar_new(dpool);

    for (int i = 1; i < argc; ++i)
        cookie_jar_set_cookie2(jar, argv[i], "foo.bar", NULL);

    struct strmap *headers = strmap_new(pool, 4);
    cookie_jar_http_header(jar, "foo.bar", "/x", headers, pool);

    const struct growing_buffer *gb = headers_dup(pool, headers);
    struct growing_buffer_reader reader;
    growing_buffer_reader_init(&reader, gb);

    const void *data;
    size_t length;
    while ((data = growing_buffer_reader_read(&reader, &length)) != NULL) {
        ssize_t nbytes = write(1, data, length);
        if (nbytes < 0) {
            perror("write() failed");
            return 1;
        }

        if (nbytes == 0)
            break;

        growing_buffer_reader_consume(&reader, (size_t)nbytes);
    }

    dpool_destroy(dpool);
    shm_close(shm);

    tpool_deinit();
    pool_unref(pool);
    pool_commit();
    pool_recycler_clear();
}
