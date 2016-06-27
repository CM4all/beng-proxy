#include "cookie_client.hxx"
#include "cookie_jar.hxx"
#include "header_writer.hxx"
#include "RootPool.hxx"
#include "shm/shm.hxx"
#include "shm/dpool.hxx"
#include "strmap.hxx"
#include "growing_buffer.hxx"
#include "util/ConstBuffer.hxx"

#include <stdio.h>
#include <unistd.h>
#include <string.h>

int main(int argc, char **argv) {
    RootPool pool;

    struct shm *shm = shm_new(1024, 512);
    struct dpool *dpool = dpool_new(*shm);

    CookieJar jar(*dpool);

    for (int i = 1; i < argc; ++i)
        cookie_jar_set_cookie2(&jar, argv[i], "foo.bar", nullptr);

    StringMap *headers = strmap_new(pool);
    cookie_jar_http_header(&jar, "foo.bar", "/x", headers, pool);

    const GrowingBuffer gb = headers_dup(pool, headers);
    GrowingBufferReader reader(gb);

    ConstBuffer<void> src;
    while (!(src = reader.Read()).IsNull()) {
        ssize_t nbytes = write(1, src.data, src.size);
        if (nbytes < 0) {
            perror("write() failed");
            return 1;
        }

        if (nbytes == 0)
            break;

        reader.Consume((size_t)nbytes);
    }

    dpool_destroy(dpool);
    shm_close(shm);
}
