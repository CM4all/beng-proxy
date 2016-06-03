#include "cookie_client.hxx"
#include "cookie_jar.hxx"
#include "header_writer.hxx"
#include "RootPool.hxx"
#include "shm/shm.hxx"
#include "shm/dpool.hxx"
#include "strmap.hxx"

#include <assert.h>
#include <stdio.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>

static void
Test1(struct dpool *dpool)
{
    RootPool pool;
    struct strmap *headers = strmap_new(pool);

    CookieJar jar(*dpool);

    /* empty cookie jar */
    cookie_jar_http_header(&jar, "foo.bar", "/", headers, pool);
    assert(headers->Get("cookie") == nullptr);
    assert(headers->Get("cookie2") == nullptr);

    /* wrong domain */
    cookie_jar_set_cookie2(&jar, "a=b", "other.domain", nullptr);
    cookie_jar_http_header(&jar, "foo.bar", "/", headers, pool);
    assert(headers->Get("cookie") == nullptr);
    assert(headers->Get("cookie2") == nullptr);

    /* correct domain */
    cookie_jar_set_cookie2(&jar, "a=b", "foo.bar", nullptr);
    cookie_jar_http_header(&jar, "foo.bar", "/", headers, pool);
    assert(strcmp(headers->Get("cookie"), "a=b") == 0);

    /* another cookie */
    headers = strmap_new(pool);
    cookie_jar_set_cookie2(&jar, "c=d", "foo.bar", nullptr);
    cookie_jar_http_header(&jar, "foo.bar", "/", headers, pool);
    assert(strcmp(headers->Get("cookie"), "c=d; a=b") == 0);

    /* delete a cookie */
    headers = strmap_new(pool);
    cookie_jar_set_cookie2(&jar, "c=xyz;max-age=0", "foo.bar", nullptr);
    cookie_jar_http_header(&jar, "foo.bar", "/", headers, pool);
    assert(strcmp(headers->Get("cookie"), "a=b") == 0);

    /* other domain */
    headers = strmap_new(pool);
    cookie_jar_http_header(&jar, "other.domain", "/some_path", headers, pool);
    assert(strcmp(headers->Get("cookie"), "a=b") == 0);
}

static void
Test2(struct dpool *dpool)
{
    RootPool pool;
    struct strmap *headers = strmap_new(pool);

    /* wrong path */
    CookieJar jar(*dpool);

    headers = strmap_new(pool);
    cookie_jar_set_cookie2(&jar, "a=b;path=\"/foo\"", "foo.bar", "/bar/x");
    cookie_jar_http_header(&jar, "foo.bar", "/", headers, pool);
    assert(headers->Get("cookie") == nullptr);
    assert(headers->Get("cookie2") == nullptr);

    /* correct path */
    headers = strmap_new(pool);
    cookie_jar_set_cookie2(&jar, "a=b;path=\"/bar\"", "foo.bar", "/bar/x");
    cookie_jar_http_header(&jar, "foo.bar", "/bar", headers, pool);
    assert(strcmp(headers->Get("cookie"), "a=b") == 0);

    /* delete: path mismatch */
    headers = strmap_new(pool);
    cookie_jar_set_cookie2(&jar, "a=b;path=\"/foo\";max-age=0",
                           "foo.bar", "/foo/x");
    cookie_jar_http_header(&jar, "foo.bar", "/bar", headers, pool);
    assert(strcmp(headers->Get("cookie"), "a=b") == 0);

    /* delete: path match */
    headers = strmap_new(pool);
    cookie_jar_set_cookie2(&jar, "a=b;path=\"/bar\";max-age=0",
                           "foo.bar", "/bar/x");
    cookie_jar_http_header(&jar, "foo.bar", "/bar", headers, pool);
    assert(headers->Get("cookie") == nullptr);
    assert(headers->Get("cookie2") == nullptr);
}

int main(int argc gcc_unused, char **argv gcc_unused) {
    struct shm *shm;
    struct dpool *dpool;

    shm = shm_new(1024, 512);
    dpool = dpool_new(*shm);

    Test1(dpool);
    Test2(dpool);

    dpool_destroy(dpool);
    shm_close(shm);
}
