#include "cookie-client.h"
#include "header-writer.h"
#include "tpool.h"
#include "shm.h"
#include "dpool.h"
#include "strmap.h"

#include <assert.h>
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
    cookie_jar_set_cookie2(jar, "a=b", "other.domain", NULL);
    cookie_jar_http_header(jar, "foo.bar", "/", headers, pool);
    assert(strmap_get(headers, "cookie") == NULL);
    assert(strmap_get(headers, "cookie2") == NULL);

    /* correct domain */
    cookie_jar_set_cookie2(jar, "a=b", "foo.bar", NULL);
    cookie_jar_http_header(jar, "foo.bar", "/", headers, pool);
    assert(strcmp(strmap_get(headers, "cookie"), "a=b") == 0);

    /* another cookie */
    headers = strmap_new(pool, 4);
    cookie_jar_set_cookie2(jar, "c=d", "foo.bar", NULL);
    cookie_jar_http_header(jar, "foo.bar", "/", headers, pool);
    assert(strcmp(strmap_get(headers, "cookie"), "c=d; a=b") == 0);

    /* delete a cookie */
    headers = strmap_new(pool, 4);
    cookie_jar_set_cookie2(jar, "c=xyz;max-age=0", "foo.bar", NULL);
    cookie_jar_http_header(jar, "foo.bar", "/", headers, pool);
    assert(strcmp(strmap_get(headers, "cookie"), "a=b") == 0);

    /* other domain */
    headers = strmap_new(pool, 4);
    cookie_jar_http_header(jar, "other.domain", "/some_path", headers, pool);
    assert(strcmp(strmap_get(headers, "cookie"), "a=b") == 0);

    /* wrong path */
    jar = cookie_jar_new(dpool);
    headers = strmap_new(pool, 4);
    cookie_jar_set_cookie2(jar, "a=b;path=\"/foo\"", "foo.bar", "/bar/x");
    cookie_jar_http_header(jar, "foo.bar", "/", headers, pool);
    assert(strmap_get(headers, "cookie") == NULL);
    assert(strmap_get(headers, "cookie2") == NULL);

    /* correct path */
    headers = strmap_new(pool, 4);
    cookie_jar_set_cookie2(jar, "a=b;path=\"/bar\"", "foo.bar", "/bar/x");
    cookie_jar_http_header(jar, "foo.bar", "/bar", headers, pool);
    assert(strcmp(strmap_get(headers, "cookie"), "a=b") == 0);

    /* delete: path mismatch */
    headers = strmap_new(pool, 4);
    cookie_jar_set_cookie2(jar, "a=b;path=\"/foo\";max-age=0",
                           "foo.bar", "/foo/x");
    cookie_jar_http_header(jar, "foo.bar", "/bar", headers, pool);
    assert(strcmp(strmap_get(headers, "cookie"), "a=b") == 0);

    /* delete: path match */
    headers = strmap_new(pool, 4);
    cookie_jar_set_cookie2(jar, "a=b;path=\"/bar\";max-age=0",
                           "foo.bar", "/bar/x");
    cookie_jar_http_header(jar, "foo.bar", "/bar", headers, pool);
    assert(strmap_get(headers, "cookie") == NULL);
    assert(strmap_get(headers, "cookie2") == NULL);

    dpool_destroy(dpool);
    shm_close(shm);

    tpool_deinit();
    pool_unref(pool);
    pool_commit();
    pool_recycler_clear();
}
