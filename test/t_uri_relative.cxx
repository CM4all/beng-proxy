#include "uri/uri_relative.hxx"
#include "puri_relative.hxx"
#include "RootPool.hxx"
#include "util/StringView.hxx"

#include <inline/compiler.h>

#include <assert.h>
#include <string.h>

int
main(gcc_unused int argc, gcc_unused char **argv)
{
    RootPool pool;

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

    assert(strcmp(uri_absolute(pool, "http://localhost/", "foo"),
                  "http://localhost/foo") == 0);
    assert(strcmp(uri_absolute(pool, "http://localhost/bar", "foo"),
                  "http://localhost/foo") == 0);
    assert(strcmp(uri_absolute(pool, "http://localhost/bar/", "foo"),
                  "http://localhost/bar/foo") == 0);
    assert(strcmp(uri_absolute(pool, "http://localhost/bar/", "/foo"),
                  "http://localhost/foo") == 0);
    assert(strcmp(uri_absolute(pool, "http://localhost/bar/",
                               "http://localhost/bar/foo"),
                  "http://localhost/bar/foo") == 0);
    assert(strcmp(uri_absolute(pool, "http://localhost/bar/",
                               "http://localhost/foo"),
                  "http://localhost/foo") == 0);
    assert(strcmp(uri_absolute(pool, "http://localhost", "foo"),
                  "http://localhost/foo") == 0);
    assert(strcmp(uri_absolute(pool, "/", "foo"), "/foo") == 0);
    assert(strcmp(uri_absolute(pool, "/bar", "foo"), "/foo") == 0);
    assert(strcmp(uri_absolute(pool, "/bar/", "foo"), "/bar/foo") == 0);
    assert(strcmp(uri_absolute(pool, "/bar/", "/foo"), "/foo") == 0);
    assert(strcmp(uri_absolute(pool, "/bar", "?foo"), "/bar?foo") == 0);

    assert(strcmp(uri_absolute(pool, "http://localhost/foo/",
                               "//example.com/bar"),
                  "http://example.com/bar") == 0);

    assert(strcmp(uri_absolute(pool, "ftp://localhost/foo/",
                               "//example.com/bar"),
                  "ftp://example.com/bar") == 0);

    assert(strcmp(uri_absolute(pool, "/foo/", "//example.com/bar"),
                  "//example.com/bar") == 0);

    assert(strcmp(uri_absolute(pool, "//example.com/foo/", "bar"),
                  "//example.com/foo/bar") == 0);

    assert(strcmp(uri_absolute(pool, "//example.com/foo/", "/bar"),
                  "//example.com/bar") == 0);

    assert(strcmp(uri_absolute(pool, "//example.com", "bar"),
                  "//example.com/bar") == 0);

    assert(strcmp(uri_absolute(pool, "//example.com", "/bar"),
                  "//example.com/bar") == 0);
}
