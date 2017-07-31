#include "uri/uri_escape.hxx"
#include "util/StringView.hxx"
#include "util/Compiler.h"

#include <gtest/gtest.h>

#include <string.h>
#include <stdlib.h>

static constexpr struct UriEscapeData {
    const char *escaped, *unescaped;
} uri_escape_data[] = {
    { "", "" },
    { "%20", " " },
    { "%ff", "\xff" },
    { "%00", nullptr },
    { "%", nullptr },
    { "%1", nullptr },
    { "%gg", nullptr },
    { "foo", "foo" },
    { "foo%20bar", "foo bar" },
    { "foo%25bar", "foo%bar" },
    { "foo%2525bar", "foo%25bar" },
};

TEST(UriEscapeTest, Escape)
{
    for (auto i : uri_escape_data) {
        if (i.unescaped == nullptr)
            continue;

        char buffer[256];
        size_t length = uri_escape(buffer, i.unescaped);
        ASSERT_EQ(length, strlen(i.escaped));
        ASSERT_EQ(memcmp(buffer, i.escaped, length), 0);
    }
}

TEST(UriEscapeTest, Unescape)
{
    for (auto i : uri_escape_data) {
        char buffer[256];
        strcpy(buffer, i.escaped);

        auto result = uri_unescape(buffer, i.escaped);
        if (i.unescaped == nullptr) {
            ASSERT_EQ(result, (char *)nullptr);
        } else {
            size_t length = result - buffer;
            ASSERT_EQ(length, strlen(i.unescaped));
            ASSERT_EQ(memcmp(buffer, i.unescaped, length), 0);
        }
    }
}
