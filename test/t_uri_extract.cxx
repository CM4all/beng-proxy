#include "uri/uri_extract.hxx"
#include "util/StringView.hxx"
#include "util/Compiler.h"

#include <gtest/gtest.h>

#include <string.h>
#include <stdlib.h>

static constexpr struct UriTests {
    const char *uri;
    const char *host_and_port;
    const char *path;
    const char *query_string;
} uri_tests[] = {
    { "http://foo/bar", "foo", "/bar", nullptr },
    { "https://foo/bar", "foo", "/bar", nullptr },
    { "ajp://foo/bar", "foo", "/bar", nullptr },
    { "http://foo:8080/bar", "foo:8080", "/bar", nullptr },
    { "http://foo", "foo", nullptr, nullptr },
    { "http://foo/bar?a=b", "foo", "/bar?a=b", "a=b" },
    { "whatever-scheme://foo/bar?a=b", "foo", "/bar?a=b", "a=b" },
    { "//foo/bar", "foo", "/bar", nullptr },
    { "//foo", "foo", nullptr, nullptr },
    { "/bar?a=b", nullptr, "/bar?a=b", "a=b" },
    { "bar?a=b", nullptr, "bar?a=b", "a=b" },
};

TEST(UriExtractTest, HostAndPort)
{
    for (auto i : uri_tests) {
        auto result = uri_host_and_port(i.uri);
        if (i.host_and_port == nullptr) {
            ASSERT_EQ(i.host_and_port, result.data);
            ASSERT_EQ(result.size, size_t(0));
        } else {
            ASSERT_NE(result.data, nullptr);
            ASSERT_EQ(result.size, strlen(i.host_and_port));
            ASSERT_EQ(memcmp(i.host_and_port, result.data,
                                        result.size), 0);
        }
    }
}

TEST(UriExtractTest, Path)
{
    for (auto i : uri_tests) {
        auto result = uri_path(i.uri);
        if (i.path == nullptr)
            ASSERT_EQ(i.path, result);
        else
            ASSERT_EQ(strcmp(i.path, result), 0);
    }
}

TEST(UriExtractTest, QueryString)
{
    for (auto i : uri_tests) {
        auto result = uri_query_string(i.uri);
        if (i.query_string == nullptr)
            ASSERT_EQ(i.query_string, result);
        else
            ASSERT_EQ(strcmp(i.query_string, result), 0);
    }
}
