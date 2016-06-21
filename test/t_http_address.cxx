#include "http_address.hxx"
#include "RootPool.hxx"
#include "util/StringView.hxx"

#include <assert.h>
#include <string.h>

static void
test_unix(struct pool *pool)
{
    auto *a = http_address_parse(pool, "unix:/var/run/foo", NULL);
    assert(a != NULL);
    assert(a->host_and_port == NULL);
    assert(strcmp(a->path, "/var/run/foo") == 0);
}

static void
test_apply(struct pool *pool)
{
    auto *a = http_address_parse(pool, "http://localhost/foo", NULL);
    assert(a != NULL);
    assert(a->scheme == URI_SCHEME_HTTP);
    assert(a->host_and_port != NULL);
    assert(strcmp(a->host_and_port, "localhost") == 0);
    assert(strcmp(a->path, "/foo") == 0);

    const auto *b = a->Apply(pool, "");
    assert(b != NULL);
    assert(b->scheme == a->scheme);
    assert(strcmp(b->host_and_port, a->host_and_port) == 0);
    assert(strcmp(b->path, "/foo") == 0);

    b = a->Apply(pool, "bar");
    assert(b != NULL);
    assert(b->scheme == a->scheme);
    assert(strcmp(b->host_and_port, a->host_and_port) == 0);
    assert(strcmp(b->path, "/bar") == 0);

    b = a->Apply(pool, "/");
    assert(b != NULL);
    assert(b->scheme == a->scheme);
    assert(strcmp(b->host_and_port, a->host_and_port) == 0);
    assert(strcmp(b->path, "/") == 0);

    b = a->Apply(pool, "http://example.com/");
    assert(b == NULL);

    b = a->Apply(pool, "http://localhost/bar");
    assert(b != NULL);
    assert(b->scheme == a->scheme);
    assert(strcmp(b->host_and_port, a->host_and_port) == 0);
    assert(strcmp(b->path, "/bar") == 0);

    b = a->Apply(pool, "?query");
    assert(b != NULL);
    assert(b->scheme == a->scheme);
    assert(strcmp(b->host_and_port, a->host_and_port) == 0);
    assert(strcmp(b->path, "/foo?query") == 0);
}

int
main(gcc_unused int argc, gcc_unused char **argv)
{
    test_unix(RootPool());
    test_apply(RootPool());
}
