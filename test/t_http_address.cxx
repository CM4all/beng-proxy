#include "http_address.hxx"
#include "RootPool.hxx"
#include "AllocatorPtr.hxx"
#include "util/StringView.hxx"

#include <assert.h>
#include <string.h>

static void
test_unix(AllocatorPtr alloc)
{
    auto *a = http_address_parse(alloc, "unix:/var/run/foo");
    assert(a != NULL);
    assert(a->host_and_port == NULL);
    assert(strcmp(a->path, "/var/run/foo") == 0);
}

static void
test_apply(AllocatorPtr alloc)
{
    auto *a = http_address_parse(alloc, "http://localhost/foo");
    assert(a != NULL);
    assert(a->protocol == HttpAddress::Protocol::HTTP);
    assert(a->host_and_port != NULL);
    assert(strcmp(a->host_and_port, "localhost") == 0);
    assert(strcmp(a->path, "/foo") == 0);

    const auto *b = a->Apply(alloc, "");
    assert(b != NULL);
    assert(b->protocol == a->protocol);
    assert(strcmp(b->host_and_port, a->host_and_port) == 0);
    assert(strcmp(b->path, "/foo") == 0);

    b = a->Apply(alloc, "bar");
    assert(b != NULL);
    assert(b->protocol == a->protocol);
    assert(strcmp(b->host_and_port, a->host_and_port) == 0);
    assert(strcmp(b->path, "/bar") == 0);

    b = a->Apply(alloc, "/");
    assert(b != NULL);
    assert(b->protocol == a->protocol);
    assert(strcmp(b->host_and_port, a->host_and_port) == 0);
    assert(strcmp(b->path, "/") == 0);

    b = a->Apply(alloc, "http://example.com/");
    assert(b == NULL);

    b = a->Apply(alloc, "http://localhost/bar");
    assert(b != NULL);
    assert(b->protocol == a->protocol);
    assert(strcmp(b->host_and_port, a->host_and_port) == 0);
    assert(strcmp(b->path, "/bar") == 0);

    b = a->Apply(alloc, "?query");
    assert(b != NULL);
    assert(b->protocol == a->protocol);
    assert(strcmp(b->host_and_port, a->host_and_port) == 0);
    assert(strcmp(b->path, "/foo?query") == 0);
}

int
main(gcc_unused int argc, gcc_unused char **argv)
{
    test_unix(AllocatorPtr(RootPool()));
    test_apply(AllocatorPtr(RootPool()));
}
