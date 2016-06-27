#include "cookie_server.hxx"
#include "header_writer.hxx"
#include "strmap.hxx"
#include "RootPool.hxx"

#include <assert.h>
#include <string.h>
#include <stdio.h>

int main(int argc gcc_unused, char **argv gcc_unused) {
    RootPool pool;

    {
        const auto cookies = cookie_map_parse(pool, "a=b");
        assert(strcmp(cookies.Get("a"), "b") == 0);
    }

    {
        const auto cookies = cookie_map_parse(pool, "c=d;e=f");
        assert(strcmp(cookies.Get("c"), "d") == 0);
        assert(strcmp(cookies.Get("e"), "f") == 0);
    }

    {
        const auto cookies = cookie_map_parse(pool, "quoted=\"quoted!\\\\");
        assert(strcmp(cookies.Get("quoted"), "quoted!\\") == 0);
    }

    {
        const auto cookies = cookie_map_parse(pool, "invalid1=foo\t");
        assert(strcmp(cookies.Get("invalid1"), "foo") == 0);
    }

    {
        /* this is actually invalid, but unfortunately RFC ignorance
           is viral, and forces us to accept square brackets :-( */
        const auto cookies = cookie_map_parse(pool, "invalid2=foo |[bar] ,");
        assert(strcmp(cookies.Get("invalid2"), "foo |[bar] ,") == 0);
    }

    assert(strcmp(cookie_exclude("foo=\"bar\"", "abc", pool),
                  "foo=\"bar\"") == 0);

    assert(cookie_exclude("foo=\"bar\"", "foo", pool) == nullptr);

    assert(strcmp(cookie_exclude("a=\"b\"", "foo", pool),
                  "a=\"b\"") == 0);

    assert(strcmp(cookie_exclude("a=b", "foo", pool),
                  "a=b") == 0);

    assert(strcmp(cookie_exclude("a=\"b\"; foo=\"bar\"; c=\"d\"", "foo", pool),
                  "a=\"b\"; c=\"d\"") == 0);

    assert(strcmp(cookie_exclude("foo=\"bar\"; c=\"d\"", "foo", pool),
                  "c=\"d\"") == 0);

    assert(strcmp(cookie_exclude("a=\"b\"; foo=\"bar\"", "foo", pool),
                  "a=\"b\"; ") == 0);

    assert(strcmp(cookie_exclude("foo=\"duplicate\"; a=\"b\"; foo=\"bar\"; c=\"d\"", "foo", pool),
                  "a=\"b\"; c=\"d\"") == 0);
}
