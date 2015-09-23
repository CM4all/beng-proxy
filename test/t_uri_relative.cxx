#include "uri/uri_relative.hxx"
#include "puri_relative.hxx"
#include "pool.hxx"

#include <inline/compiler.h>

#include <assert.h>
#include <string.h>

int
main(gcc_unused int argc, gcc_unused char **argv)
{
    struct pool *pool;

    pool = pool_new_libc(nullptr, "root");

    assert(strcmp(uri_compress(pool, "/foo/bar"), "/foo/bar") == 0);
    assert(strcmp(uri_compress(pool, "/foo/./bar"), "/foo/bar") == 0);
    assert(strcmp(uri_compress(pool, "/./foo/bar"), "/foo/bar") == 0);
    assert(strcmp(uri_compress(pool, "/foo/bar/./"), "/foo/bar/") == 0);
    assert(strcmp(uri_compress(pool, "./foo/bar/"), "foo/bar/") == 0);
    assert(strcmp(uri_compress(pool, "/foo//bar/"), "/foo/bar/") == 0);
    assert(strcmp(uri_compress(pool, "/foo///bar/"), "/foo/bar/") == 0);
    assert(strcmp(uri_compress(pool, "/1/2/../3/"), "/1/3/") == 0);
    assert(strcmp(uri_compress(pool, "/1/2/../../3/"), "/3/") == 0);
    assert(strcmp(uri_compress(pool, "foo/../bar"), "bar") == 0);
    assert(strcmp(uri_compress(pool, "foo//../bar"), "bar") == 0);
    assert(strcmp(uri_compress(pool, "foo/.."), "") == 0);
    assert(strcmp(uri_compress(pool, "foo/../."), "") == 0);

    assert(uri_compress(pool, "/1/2/../../../3/") == nullptr);
    assert(uri_compress(pool, "/../") == nullptr);
    assert(uri_compress(pool, "/a/../../") == nullptr);
    assert(uri_compress(pool, "/..") == nullptr);
    assert(uri_compress(pool, "..") == nullptr);
    assert(strcmp(uri_compress(pool, "/1/2/.."), "/1/") == 0);

    assert(strcmp(uri_absolute(pool, "http://localhost/", "foo", 3),
                  "http://localhost/foo") == 0);
    assert(strcmp(uri_absolute(pool, "http://localhost/bar", "foo", 3),
                  "http://localhost/foo") == 0);
    assert(strcmp(uri_absolute(pool, "http://localhost/bar/", "foo", 3),
                  "http://localhost/bar/foo") == 0);
    assert(strcmp(uri_absolute(pool, "http://localhost/bar/", "/foo", 4),
                  "http://localhost/foo") == 0);
    assert(strcmp(uri_absolute(pool, "http://localhost/bar/",
                               "http://localhost/bar/foo", 24),
                  "http://localhost/bar/foo") == 0);
    assert(strcmp(uri_absolute(pool, "http://localhost/bar/",
                               "http://localhost/foo", 24),
                  "http://localhost/foo") == 0);
    assert(strcmp(uri_absolute(pool, "http://localhost", "foo", 3),
                  "http://localhost/foo") == 0);
    assert(strcmp(uri_absolute(pool, "/", "foo", 3), "/foo") == 0);
    assert(strcmp(uri_absolute(pool, "/bar", "foo", 3), "/foo") == 0);
    assert(strcmp(uri_absolute(pool, "/bar/", "foo", 3), "/bar/foo") == 0);
    assert(strcmp(uri_absolute(pool, "/bar/", "/foo", 4), "/foo") == 0);
    assert(strcmp(uri_absolute(pool, "/bar", "?foo", 4), "/bar?foo") == 0);

    assert(strcmp(uri_absolute(pool, "http://localhost/foo/",
                               "//example.com/bar", 17),
                  "http://example.com/bar") == 0);

    assert(strcmp(uri_absolute(pool, "ftp://localhost/foo/",
                               "//example.com/bar", 17),
                  "ftp://example.com/bar") == 0);

    assert(strcmp(uri_absolute(pool, "/foo/", "//example.com/bar", 17),
                  "//example.com/bar") == 0);

    assert(strcmp(uri_absolute(pool, "//example.com/foo/", "bar", 3),
                  "//example.com/foo/bar") == 0);

    assert(strcmp(uri_absolute(pool, "//example.com/foo/", "/bar", 4),
                  "//example.com/bar") == 0);

    assert(strcmp(uri_absolute(pool, "//example.com", "bar", 3),
                  "//example.com/bar") == 0);

    assert(strcmp(uri_absolute(pool, "//example.com", "/bar", 4),
                  "//example.com/bar") == 0);

    pool_unref(pool);
    pool_commit();
    pool_recycler_clear();
}
