#include "uri-relative.h"

#include <inline/compiler.h>

#include <assert.h>
#include <string.h>

int main(int argc __attr_unused, char **argv __attr_unused) {
    pool_t pool;

    pool = pool_new_libc(NULL, "root");

    assert(strcmp(uri_compress(pool, "/foo/bar"), "/foo/bar") == 0);
    assert(strcmp(uri_compress(pool, "/foo/./bar"), "/foo/bar") == 0);
    assert(strcmp(uri_compress(pool, "/./foo/bar"), "/foo/bar") == 0);
    assert(strcmp(uri_compress(pool, "/foo/bar/./"), "/foo/bar/") == 0);
    assert(strcmp(uri_compress(pool, "./foo/bar/"), "foo/bar/") == 0);
    assert(strcmp(uri_compress(pool, "/foo//bar/"), "/foo/bar/") == 0);
    assert(strcmp(uri_compress(pool, "/foo///bar/"), "/foo/bar/") == 0);
    assert(strcmp(uri_compress(pool, "/1/2/../3/"), "/1/3/") == 0);
    assert(strcmp(uri_compress(pool, "/1/2/../../3/"), "/3/") == 0);
    assert(uri_compress(pool, "/1/2/../../../3/") == NULL);
    assert(uri_compress(pool, "/../") == NULL);
    assert(uri_compress(pool, "/a/../../") == NULL);
    assert(uri_compress(pool, "/..") == NULL);
    assert(uri_compress(pool, "..") == NULL);
    assert(strcmp(uri_compress(pool, "/1/2/.."), "/1/") == 0);

    pool_unref(pool);
    pool_commit();
    pool_recycler_clear();
}
