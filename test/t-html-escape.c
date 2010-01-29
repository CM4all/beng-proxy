#include "html-escape.h"
#include "strref.h"

int
main(int argc, char **argv)
{
    struct strref s;
    size_t used;

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

    strref_set_c(&s, "&gt&lt;");
    used = html_unescape(&s);
    assert(used == 7);
    assert(strref_cmp_literal(&s, "&gt<") == 0);

    return 0;
}
