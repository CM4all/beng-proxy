#include "html-escape.h"
#include "strref.h"

int
main(int argc, char **argv)
{
    struct strref s;
    size_t used, length;

    (void)argc;
    (void)argv;

    strref_set_c(&s, "foo bar");
    used = html_unescape(&s);
    assert(used == 0);

    strref_set_c(&s, "foo&amp;bar");
    used = html_unescape(&s);
    assert(used == 11);
    assert(strref_cmp_literal(&s, "foo&bar") == 0);

    strref_set_c(&s, "&lt;&gt;");
    used = html_unescape(&s);
    assert(used == 8);
    assert(strref_cmp_literal(&s, "<>") == 0);

    strref_set_c(&s, "&quot");
    used = html_unescape(&s);
    assert(used == 0);

    strref_set_c(&s, "&amp;amp;");
    used = html_unescape(&s);
    assert(used == 9);
    assert(strref_cmp_literal(&s, "&amp;") == 0);

    strref_set_c(&s, "&amp;&&quot;");
    used = html_unescape(&s);
    assert(used == 12);
    assert(strref_cmp_literal(&s, "&&\"") == 0);

    strref_set_c(&s, "&gt&lt;&apos;");
    used = html_unescape(&s);
    assert(used == 13);
    assert(strref_cmp_literal(&s, "&gt<'") == 0);

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
