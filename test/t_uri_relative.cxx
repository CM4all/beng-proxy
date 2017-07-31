#include "uri/uri_relative.hxx"
#include "puri_relative.hxx"
#include "TestPool.hxx"
#include "AllocatorPtr.hxx"
#include "util/StringView.hxx"

#include "util/Compiler.h"

#include <assert.h>
#include <string.h>

int
main(gcc_unused int argc, gcc_unused char **argv)
{
    TestPool pool;
    AllocatorPtr alloc(pool);

    assert(strcmp(uri_compress(alloc, "/foo/bar"), "/foo/bar") == 0);
    assert(strcmp(uri_compress(alloc, "/foo/./bar"), "/foo/bar") == 0);
    assert(strcmp(uri_compress(alloc, "/./foo/bar"), "/foo/bar") == 0);
    assert(strcmp(uri_compress(alloc, "/foo/bar/./"), "/foo/bar/") == 0);
    assert(strcmp(uri_compress(alloc, "./foo/bar/"), "foo/bar/") == 0);
    assert(strcmp(uri_compress(alloc, "/foo//bar/"), "/foo/bar/") == 0);
    assert(strcmp(uri_compress(alloc, "/foo///bar/"), "/foo/bar/") == 0);
    assert(strcmp(uri_compress(alloc, "/1/2/../3/"), "/1/3/") == 0);
    assert(strcmp(uri_compress(alloc, "/1/2/../../3/"), "/3/") == 0);
    assert(strcmp(uri_compress(alloc, "foo/../bar"), "bar") == 0);
    assert(strcmp(uri_compress(alloc, "foo//../bar"), "bar") == 0);
    assert(strcmp(uri_compress(alloc, "foo/.."), "") == 0);
    assert(strcmp(uri_compress(alloc, "foo/../."), "") == 0);

    assert(uri_compress(alloc, "/1/2/../../../3/") == nullptr);
    assert(uri_compress(alloc, "/../") == nullptr);
    assert(uri_compress(alloc, "/a/../../") == nullptr);
    assert(uri_compress(alloc, "/..") == nullptr);
    assert(uri_compress(alloc, "..") == nullptr);
    assert(strcmp(uri_compress(alloc, "/1/2/.."), "/1/") == 0);

    assert(strcmp(uri_absolute(alloc, "http://localhost/", "foo"),
                  "http://localhost/foo") == 0);
    assert(strcmp(uri_absolute(alloc, "http://localhost/bar", "foo"),
                  "http://localhost/foo") == 0);
    assert(strcmp(uri_absolute(alloc, "http://localhost/bar/", "foo"),
                  "http://localhost/bar/foo") == 0);
    assert(strcmp(uri_absolute(alloc, "http://localhost/bar/", "/foo"),
                  "http://localhost/foo") == 0);
    assert(strcmp(uri_absolute(alloc, "http://localhost/bar/",
                               "http://localhost/bar/foo"),
                  "http://localhost/bar/foo") == 0);
    assert(strcmp(uri_absolute(alloc, "http://localhost/bar/",
                               "http://localhost/foo"),
                  "http://localhost/foo") == 0);
    assert(strcmp(uri_absolute(alloc, "http://localhost", "foo"),
                  "http://localhost/foo") == 0);
    assert(strcmp(uri_absolute(alloc, "/", "foo"), "/foo") == 0);
    assert(strcmp(uri_absolute(alloc, "/bar", "foo"), "/foo") == 0);
    assert(strcmp(uri_absolute(alloc, "/bar/", "foo"), "/bar/foo") == 0);
    assert(strcmp(uri_absolute(alloc, "/bar/", "/foo"), "/foo") == 0);
    assert(strcmp(uri_absolute(alloc, "/bar", "?foo"), "/bar?foo") == 0);

    assert(strcmp(uri_absolute(alloc, "http://localhost/foo/",
                               "//example.com/bar"),
                  "http://example.com/bar") == 0);

    assert(strcmp(uri_absolute(alloc, "ftp://localhost/foo/",
                               "//example.com/bar"),
                  "ftp://example.com/bar") == 0);

    assert(strcmp(uri_absolute(alloc, "/foo/", "//example.com/bar"),
                  "//example.com/bar") == 0);

    assert(strcmp(uri_absolute(alloc, "//example.com/foo/", "bar"),
                  "//example.com/foo/bar") == 0);

    assert(strcmp(uri_absolute(alloc, "//example.com/foo/", "/bar"),
                  "//example.com/bar") == 0);

    assert(strcmp(uri_absolute(alloc, "//example.com", "bar"),
                  "//example.com/bar") == 0);

    assert(strcmp(uri_absolute(alloc, "//example.com", "/bar"),
                  "//example.com/bar") == 0);
}
