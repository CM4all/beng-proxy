#include "escape_css.h"
#include "escape_class.h"

#include <string.h>

static void
check_unescape_find(const char *p, size_t offset)
{
    assert(unescape_find(&css_escape_class, p, strlen(p)) == p + offset);
}

static void
check_unescape(const char *p, const char *q)
{
    static char buffer[1024];
    size_t l = unescape_buffer(&css_escape_class, p, strlen(p), buffer);
    assert(l == strlen(q));
    assert(memcmp(buffer, q, l) == 0);
}

static void
check_escape_find(const char *p, size_t offset)
{
    assert(escape_find(&css_escape_class, p, strlen(p)) == p + offset);
}

static void
check_escape(const char *p, const char *q)
{
    static char buffer[1024];
    size_t l = escape_buffer(&css_escape_class, p, strlen(p), buffer);
    assert(l == strlen(q));
    assert(memcmp(buffer, q, l) == 0);
}

int
main(int argc, char **argv)
{
    (void)argc;
    (void)argv;

    assert(unescape_find(&css_escape_class, "foobar123", 9) == NULL);
    check_unescape_find("\\", 0);
    check_unescape_find("foo\\\\", 3);
    check_unescape("foo\\\\", "foo\\");

    check_escape_find("foo'bar", 3);
    check_escape_find("foo\\bar", 3);
    check_escape_find("foo\"bar", 3);

    assert(strcmp(escape_char(&css_escape_class, '\''), "\\'") == 0);
    assert(strcmp(escape_char(&css_escape_class, '\\'), "\\\\") == 0);

    check_escape("foobar", "foobar");
    check_escape("foo\\bar", "foo\\\\bar");
    check_escape("foo'bar", "foo\\'bar");

    return 0;
}
