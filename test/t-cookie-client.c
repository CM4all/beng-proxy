#include "cookie-client.h"
#include "header-writer.h"
#include "tpool.h"
#include "shm.h"
#include "dpool.h"
#include "strmap.h"

#include <stdio.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>

int main(int argc __attr_unused, char **argv __attr_unused) {
    pool_t pool;
    struct shm *shm;
    struct dpool *dpool;
    struct cookie_jar *jar;
    struct strmap *headers;

    pool = pool_new_libc(NULL, "root");
    tpool_init(pool);

    shm = shm_new(1024, 512);
    dpool = dpool_new(shm);

    jar = cookie_jar_new(dpool);

    headers = strmap_new(pool, 4);

    /* empty cookie jar */
    cookie_jar_http_header(jar, "foo.bar", "/", headers, pool);
    assert(strmap_get(headers, "cookie") == NULL);
    assert(strmap_get(headers, "cookie2") == NULL);

    /* wrong domain */
    cookie_jar_set_cookie2(jar, "a=b", "other.domain");
    cookie_jar_http_header(jar, "foo.bar", "/", headers, pool);
    assert(strmap_get(headers, "cookie") == NULL);
    assert(strmap_get(headers, "cookie2") == NULL);

    /* correct domain */
    cookie_jar_set_cookie2(jar, "a=b", "foo.bar");
    cookie_jar_http_header(jar, "foo.bar", "/", headers, pool);
    assert(strcmp(strmap_get(headers, "cookie"), "a=b") == 0);

    /* another cookie */
    headers = strmap_new(pool, 4);
    cookie_jar_set_cookie2(jar, "c=d", "foo.bar");
    cookie_jar_http_header(jar, "foo.bar", "/", headers, pool);
    assert(strcmp(strmap_get(headers, "cookie"), "c=d; a=b") == 0);

    /* other domain */
    headers = strmap_new(pool, 4);
    cookie_jar_http_header(jar, "other.domain", "/some_path", headers, pool);
    assert(strcmp(strmap_get(headers, "cookie"), "a=b") == 0);

    dpool_destroy(dpool);
    shm_close(shm);

    tpool_deinit();
    pool_unref(pool);
    pool_commit();
    pool_recycler_clear();
}
