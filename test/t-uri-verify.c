#include "uri-verify.h"

#include <inline/compiler.h>

#include <assert.h>

static void
test_uri_path_verify_paranoid(void)
{
    assert(uri_path_verify_paranoid(""));
    assert(uri_path_verify_paranoid("/"));
    assert(uri_path_verify_paranoid(" "));
    assert(!uri_path_verify_paranoid("."));
    assert(!uri_path_verify_paranoid("./"));
    assert(!uri_path_verify_paranoid("./foo"));
    assert(!uri_path_verify_paranoid(".."));
    assert(!uri_path_verify_paranoid("../"));
    assert(!uri_path_verify_paranoid("../foo"));
    assert(!uri_path_verify_paranoid(".%2e/foo"));
    assert(uri_path_verify_paranoid("foo/bar"));
    assert(!uri_path_verify_paranoid("foo/./bar"));
    assert(uri_path_verify_paranoid("foo//bar"));
    assert(!uri_path_verify_paranoid("foo/%2ebar"));
    assert(!uri_path_verify_paranoid("foo/.%2e/bar"));
    assert(!uri_path_verify_paranoid("foo/.%2e"));
    assert(!uri_path_verify_paranoid("foo/bar/.."));
    assert(!uri_path_verify_paranoid("foo/bar/../bar"));
    assert(!uri_path_verify_paranoid("f%00"));
    assert(uri_path_verify_paranoid("f%20"));
    assert(uri_path_verify_paranoid("index%2ehtml"));
}

int
main(gcc_unused int argc, gcc_unused char **argv)
{
    test_uri_path_verify_paranoid();
}
