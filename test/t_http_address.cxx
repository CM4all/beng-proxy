#include "http_address.hxx"
#include "pool.h"

#include <assert.h>
#include <string.h>

static void
test_unix(struct pool *pool)
{
    struct http_address *a =
        http_address_parse(pool, "unix:/var/run/foo", NULL);
    assert(a != NULL);
    assert(a->scheme == URI_SCHEME_UNIX);
    assert(a->host_and_port == NULL);
    assert(strcmp(a->path, "/var/run/foo") == 0);
}

static void
test_apply(struct pool *pool)
{
    struct http_address *a =
        http_address_parse(pool, "http://localhost/foo", NULL);
    assert(a != NULL);
    assert(a->scheme == URI_SCHEME_HTTP);
    assert(a->host_and_port != NULL);
    assert(strcmp(a->host_and_port, "localhost") == 0);
    assert(strcmp(a->path, "/foo") == 0);

    const struct http_address *b = a->Apply(pool, "", 0);
    assert(b != NULL);
    assert(b->scheme == a->scheme);
    assert(strcmp(b->host_and_port, a->host_and_port) == 0);
    assert(strcmp(b->path, "/foo") == 0);

    b = a->Apply(pool, "bar", 3);
    assert(b != NULL);
    assert(b->scheme == a->scheme);
    assert(strcmp(b->host_and_port, a->host_and_port) == 0);
    assert(strcmp(b->path, "/bar") == 0);

    b = a->Apply(pool, "/", 1);
    assert(b != NULL);
    assert(b->scheme == a->scheme);
    assert(strcmp(b->host_and_port, a->host_and_port) == 0);
    assert(strcmp(b->path, "/") == 0);

    b = a->Apply(pool, "http://example.com/", 29);
    assert(b == NULL);

    b = a->Apply(pool, "http://localhost/bar", 30);
    assert(b != NULL);
    assert(b->scheme == a->scheme);
    assert(strcmp(b->host_and_port, a->host_and_port) == 0);
    assert(strcmp(b->path, "/bar") == 0);

    b = a->Apply(pool, "?query", 6);
    assert(b != NULL);
    assert(b->scheme == a->scheme);
    assert(strcmp(b->host_and_port, a->host_and_port) == 0);
    assert(strcmp(b->path, "/foo?query") == 0);
}

int
main(gcc_unused int argc, gcc_unused char **argv)
{
    struct pool *pool = pool_new_libc(NULL, "root");

    test_unix(pool);
    test_apply(pool);

    pool_unref(pool);
    pool_commit();

    pool_recycler_clear();
}
