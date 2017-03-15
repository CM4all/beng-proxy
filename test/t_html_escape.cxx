#include "escape_html.hxx"
#include "escape_class.hxx"
#include "escape_static.hxx"

#include <gtest/gtest.h>

static const char *
html_unescape(const char *p)
{
    return unescape_static(&html_escape_class, p);
}

static size_t
html_unescape_inplace(char *p, size_t length)
{
    return unescape_inplace(&html_escape_class, p, length);
}

TEST(HtmlEscape, Basic)
{
    size_t length;

    ASSERT_STREQ(html_unescape("foo bar"), "foo bar");
    ASSERT_STREQ(html_unescape("foo&amp;bar"), "foo&bar");
    ASSERT_STREQ(html_unescape("&lt;&gt;"), "<>");
    ASSERT_STREQ(html_unescape("&quot;"), "\"");
    ASSERT_STREQ(html_unescape("&amp;amp;"), "&amp;");
    ASSERT_STREQ(html_unescape("&amp;&&quot;"), "&&\"");
    ASSERT_STREQ(html_unescape("&gt&lt;&apos;"), "&gt<'");

    char a[] = "foo bar";
    length = html_unescape_inplace(a, sizeof(a) - 1);
    ASSERT_EQ(length, 7);

    char e[] = "foo&amp;bar";
    length = html_unescape_inplace(e, sizeof(e) - 1);
    ASSERT_EQ(length, 7);
    ASSERT_EQ(memcmp(e, "foo&bar", 7), 0);

    char f[] = "&lt;foo&gt;bar&apos;";
    length = html_unescape_inplace(f, sizeof(f) - 1);
    ASSERT_EQ(length, 9);
    ASSERT_EQ(memcmp(f, "<foo>bar'", 9), 0);

    char b[] = "&lt;&gt;&apos;";
    length = html_unescape_inplace(b, sizeof(b) - 1);
    ASSERT_EQ(length, 3);
    ASSERT_EQ(memcmp(b, "<>'", 3), 0);

    char c[] = "&quot";
    length = html_unescape_inplace(c, sizeof(c) - 1);
    ASSERT_EQ(length, 5);
    ASSERT_EQ(memcmp(c, "&quot", 5), 0);

    char d[] = "&amp;&&quot;";
    length = html_unescape_inplace(d, sizeof(d) - 1);
    ASSERT_EQ(length, 3);
    ASSERT_EQ(memcmp(d, "&&\"", 3), 0);
}
