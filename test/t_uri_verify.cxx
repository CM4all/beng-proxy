#include "uri/uri_verify.hxx"
#include "util/Compiler.h"

#include <gtest/gtest.h>

TEST(UriVerifyTest, Paranoid)
{
    ASSERT_TRUE(uri_path_verify_paranoid(""));
    ASSERT_TRUE(uri_path_verify_paranoid("/"));
    ASSERT_TRUE(uri_path_verify_paranoid(" "));
    ASSERT_FALSE(uri_path_verify_paranoid("."));
    ASSERT_FALSE(uri_path_verify_paranoid("./"));
    ASSERT_FALSE(uri_path_verify_paranoid("./foo"));
    ASSERT_FALSE(uri_path_verify_paranoid(".."));
    ASSERT_FALSE(uri_path_verify_paranoid("../"));
    ASSERT_FALSE(uri_path_verify_paranoid("../foo"));
    ASSERT_FALSE(uri_path_verify_paranoid(".%2e/foo"));
    ASSERT_TRUE(uri_path_verify_paranoid("foo/bar"));
    ASSERT_FALSE(uri_path_verify_paranoid("foo%2fbar"));
    ASSERT_TRUE(uri_path_verify_paranoid("/foo/bar?A%2fB%00C%"));
    ASSERT_FALSE(uri_path_verify_paranoid("foo/./bar"));
    ASSERT_TRUE(uri_path_verify_paranoid("foo//bar"));
    ASSERT_FALSE(uri_path_verify_paranoid("foo/%2ebar"));
    ASSERT_FALSE(uri_path_verify_paranoid("foo/.%2e/bar"));
    ASSERT_FALSE(uri_path_verify_paranoid("foo/.%2e"));
    ASSERT_FALSE(uri_path_verify_paranoid("foo/bar/.."));
    ASSERT_FALSE(uri_path_verify_paranoid("foo/bar/../bar"));
    ASSERT_FALSE(uri_path_verify_paranoid("f%00"));
    ASSERT_TRUE(uri_path_verify_paranoid("f%20"));
    ASSERT_TRUE(uri_path_verify_paranoid("index%2ehtml"));
}
