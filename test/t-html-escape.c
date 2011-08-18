#include "escape_html.h"
#include "escape_class.h"
#include "escape_static.h"
#include "strref.h"

/*
static const char *
unescape2(struct strref *s)
{
    if (unescape_find(&html_escape_class, s->data, s->length) == NULL)
        return 0;

    assert(s->length > 0);
    const size_t old_length = s->length;

    static char buffer[1024];
    size_t n = unescape(&html_escape_class, s->data, s->length, buffer);
    assert(n != s->length || memcmp(buffer, s->data, n) != 0);
    s->data = buffer;
    s->length = n;

    return old_length;
}

static size_t
html_unescape_inplace(char *p, size_t length)
{
    return unescape_inplace(&html_escape_class, p, length);
}
*/

static const char *
html_unescape(const char *p)
{
    return unescape_static(&html_escape_class, p, strlen(p));
}

static size_t
html_unescape_inplace(char *p, size_t length)
{
    return unescape_inplace(&html_escape_class, p, length);
}

int
main(int argc, char **argv)
{
    size_t length;

    (void)argc;
    (void)argv;

    assert(strcmp(html_unescape("foo bar"), "foo bar") == 0);
    assert(strcmp(html_unescape("foo&amp;bar"), "foo&bar") == 0);
    assert(strcmp(html_unescape("&lt;&gt;"), "<>") == 0);
    assert(strcmp(html_unescape("&quot;"), "\"") == 0);
    assert(strcmp(html_unescape("&amp;amp;"), "&amp;") == 0);
    assert(strcmp(html_unescape("&amp;&&quot;"), "&&\"") == 0);
    assert(strcmp(html_unescape("&gt&lt;&apos;"), "&gt<'") == 0);

    char a[] = "foo bar";
    length = html_unescape_inplace(a, sizeof(a) - 1);
    assert(length == 7);

    char e[] = "foo&amp;bar";
    length = html_unescape_inplace(e, sizeof(e) - 1);
    assert(length == 7);
    assert(memcmp(e, "foo&bar", 7) == 0);

    char f[] = "&lt;foo&gt;bar&apos;";
    length = html_unescape_inplace(f, sizeof(f) - 1);
    assert(length == 9);
    assert(memcmp(f, "<foo>bar'", 9) == 0);

    char b[] = "&lt;&gt;&apos;";
    length = html_unescape_inplace(b, sizeof(b) - 1);
    assert(length == 3);
    assert(memcmp(b, "<>'", 3) == 0);

    char c[] = "&quot";
    length = html_unescape_inplace(c, sizeof(c) - 1);
    assert(length == 5);
    assert(memcmp(c, "&quot", 5) == 0);

    char d[] = "&amp;&&quot;";
    length = html_unescape_inplace(d, sizeof(d) - 1);
    assert(length == 3);
    assert(memcmp(d, "&&\"", 3) == 0);

    return 0;
}
