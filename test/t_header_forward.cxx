#include "header_forward.hxx"
#include "tpool.h"
#include "strmap.hxx"
#include "product.h"

#include <assert.h>
#include <string.h>

static const char *
strmap_to_string(struct strmap *map)
{
    static char buffer[4096];
    buffer[0] = 0;

    for (const auto &i : *map) {
        strcat(buffer, i.key);
        strcat(buffer, "=");
        strcat(buffer, i.value);
        strcat(buffer, ";");
    }

    return buffer;
}

static void
check_strmap(struct strmap *map, const char *p)
{
    const char *q = strmap_to_string(map);

    assert(strcmp(q, p) == 0);
}

int
main(gcc_unused int argc, gcc_unused char **argv)
{
    struct pool *pool;
    struct strmap *headers, *out;
    struct header_forward_settings settings = {
        .modes = {
            [HEADER_GROUP_IDENTITY] = HEADER_FORWARD_MANGLE,
            [HEADER_GROUP_CAPABILITIES] = HEADER_FORWARD_YES,
            [HEADER_GROUP_COOKIE] = HEADER_FORWARD_MANGLE,
            [HEADER_GROUP_FORWARD] = HEADER_FORWARD_NO,
            [HEADER_GROUP_CORS] = HEADER_FORWARD_NO,
            [HEADER_GROUP_SECURE] = HEADER_FORWARD_NO,
            [HEADER_GROUP_OTHER] = HEADER_FORWARD_NO,
        },
    };

    pool = pool_new_libc(nullptr, "root");
    tpool_init(pool);

    headers = strmap_new(pool);
    headers->Add("from", "foo");
    headers->Add("abc", "def");
    headers->Add("cookie", "a=b");
    headers->Add("content-type", "image/jpeg");
    headers->Add("accept", "text/*");
    headers->Add("via", "1.1 192.168.0.1");
    headers->Add("x-forwarded-for", "10.0.0.2");
    headers->Add("x-cm4all-beng-user", "hans");

    /* verify strmap_to_string() */
    check_strmap(headers, "abc=def;accept=text/*;"
                 "content-type=image/jpeg;cookie=a=b;from=foo;"
                 "via=1.1 192.168.0.1;"
                 "x-cm4all-beng-user=hans;"
                 "x-forwarded-for=10.0.0.2;");

    /* nullptr test */
    out = forward_request_headers(pool, nullptr,
                                  "192.168.0.2", "192.168.0.3",
                                  false, false, false, false,
                                  &settings,
                                  nullptr, nullptr, nullptr, nullptr);
    assert(strcmp(out->Remove("user-agent"), PRODUCT_TOKEN) == 0);
    check_strmap(out, "accept-charset=utf-8;"
                 "via=1.1 192.168.0.2;x-forwarded-for=192.168.0.3;");

    /* basic test */
    headers->Add("user-agent", "firesomething");
    out = forward_request_headers(pool, headers,
                                  "192.168.0.2", "192.168.0.3",
                                  false, false, false, false,
                                  &settings,
                                  nullptr, nullptr, nullptr, nullptr);
    check_strmap(out, "accept=text/*;accept-charset=utf-8;"
                 "from=foo;user-agent=firesomething;"
                 "via=1.1 192.168.0.1, 1.1 192.168.0.2;"
                 "x-forwarded-for=10.0.0.2, 192.168.0.3;");

    /* no accept-charset forwarded */
    headers->Add("accept-charset", "iso-8859-1");

    out = forward_request_headers(pool, headers,
                                  "192.168.0.2", "192.168.0.3",
                                  false, false, false, false,
                                  &settings,
                                  nullptr, nullptr, nullptr, nullptr);
    check_strmap(out, "accept=text/*;accept-charset=utf-8;"
                 "from=foo;user-agent=firesomething;"
                 "via=1.1 192.168.0.1, 1.1 192.168.0.2;"
                 "x-forwarded-for=10.0.0.2, 192.168.0.3;");

    /* now accept-charset is forwarded */
    out = forward_request_headers(pool, headers,
                                  "192.168.0.2", "192.168.0.3",
                                  false, false, true, false,
                                  &settings,
                                  nullptr, nullptr, nullptr, nullptr);
    check_strmap(out, "accept=text/*;accept-charset=iso-8859-1;"
                 "from=foo;user-agent=firesomething;"
                 "via=1.1 192.168.0.1, 1.1 192.168.0.2;"
                 "x-forwarded-for=10.0.0.2, 192.168.0.3;");

    /* with request body */
    out = forward_request_headers(pool, headers,
                                  "192.168.0.2", "192.168.0.3",
                                  false, true, false, false,
                                  &settings,
                                  nullptr, nullptr, nullptr, nullptr);
    check_strmap(out, "accept=text/*;accept-charset=utf-8;"
                 "content-type=image/jpeg;from=foo;"
                 "user-agent=firesomething;"
                 "via=1.1 192.168.0.1, 1.1 192.168.0.2;"
                 "x-forwarded-for=10.0.0.2, 192.168.0.3;");

    /* don't forward user-agent */

    settings.modes[HEADER_GROUP_CAPABILITIES] = HEADER_FORWARD_NO;
    out = forward_request_headers(pool, headers,
                                  "192.168.0.2", "192.168.0.3",
                                  false, false, false, false,
                                  &settings,
                                  nullptr, nullptr, nullptr, nullptr);
    check_strmap(out, "accept=text/*;accept-charset=utf-8;"
                 "from=foo;"
                 "via=1.1 192.168.0.1, 1.1 192.168.0.2;"
                 "x-forwarded-for=10.0.0.2, 192.168.0.3;");

    /* mangle user-agent */

    settings.modes[HEADER_GROUP_CAPABILITIES] = HEADER_FORWARD_MANGLE;
    out = forward_request_headers(pool, headers,
                                  "192.168.0.2", "192.168.0.3",
                                  false, false, false, false,
                                  &settings,
                                  nullptr, nullptr, nullptr, nullptr);
    assert(strcmp(out->Remove("user-agent"), PRODUCT_TOKEN) == 0);
    check_strmap(out, "accept=text/*;accept-charset=utf-8;"
                 "from=foo;"
                 "via=1.1 192.168.0.1, 1.1 192.168.0.2;"
                 "x-forwarded-for=10.0.0.2, 192.168.0.3;");

    /* forward via/x-forwarded-for as-is */

    settings.modes[HEADER_GROUP_CAPABILITIES] = HEADER_FORWARD_NO;
    settings.modes[HEADER_GROUP_IDENTITY] = HEADER_FORWARD_YES;

    out = forward_request_headers(pool, headers,
                                  "192.168.0.2", "192.168.0.3",
                                  false, false, false, false,
                                  &settings,
                                  nullptr, nullptr, nullptr, nullptr);
    check_strmap(out, "accept=text/*;accept-charset=utf-8;"
                 "from=foo;"
                 "via=1.1 192.168.0.1;"
                 "x-forwarded-for=10.0.0.2;");

    /* no via/x-forwarded-for */

    settings.modes[HEADER_GROUP_IDENTITY] = HEADER_FORWARD_NO;

    out = forward_request_headers(pool, headers,
                                  "192.168.0.2", "192.168.0.3",
                                  false, false, false, false,
                                  &settings,
                                  nullptr, nullptr, nullptr, nullptr);
    check_strmap(out, "accept=text/*;accept-charset=utf-8;"
                 "from=foo;");

    /* forward cookies */

    settings.modes[HEADER_GROUP_COOKIE] = HEADER_FORWARD_YES;

    out = forward_request_headers(pool, headers,
                                  "192.168.0.2", "192.168.0.3",
                                  false, false, false, false,
                                  &settings,
                                  nullptr, nullptr, nullptr, nullptr);
    check_strmap(out, "accept=text/*;accept-charset=utf-8;"
                 "cookie=a=b;"
                 "from=foo;");

    /* forward 2 cookies */

    headers->Add("cookie", "c=d");

    out = forward_request_headers(pool, headers,
                                  "192.168.0.2", "192.168.0.3",
                                  false, false, false, false,
                                  &settings,
                                  nullptr, nullptr, nullptr, nullptr);
    check_strmap(out, "accept=text/*;accept-charset=utf-8;"
                 "cookie=a=b;cookie=c=d;"
                 "from=foo;");

    /* exclude one cookie */

    settings.modes[HEADER_GROUP_COOKIE] = HEADER_FORWARD_BOTH;

    out = forward_request_headers(pool, headers,
                                  "192.168.0.2", "192.168.0.3",
                                  false, false, false, false,
                                  &settings,
                                  "c", nullptr, nullptr, nullptr);
    check_strmap(out, "accept=text/*;accept-charset=utf-8;"
                 "cookie=a=b;"
                 "from=foo;");

    /* forward other headers */

    settings.modes[HEADER_GROUP_COOKIE] = HEADER_FORWARD_NO;
    settings.modes[HEADER_GROUP_OTHER] = HEADER_FORWARD_YES;

    out = forward_request_headers(pool, headers,
                                  "192.168.0.2", "192.168.0.3",
                                  false, false, false, false,
                                  &settings,
                                  nullptr, nullptr, nullptr, nullptr);
    check_strmap(out, "abc=def;accept=text/*;accept-charset=utf-8;"
                 "from=foo;");

    /* forward CORS headers */

    headers->Add("access-control-request-method", "POST");
    headers->Add("origin", "example.com");

    out = forward_request_headers(pool, headers,
                                  "192.168.0.2", "192.168.0.3",
                                  false, false, false, false,
                                  &settings,
                                  nullptr, nullptr, nullptr, nullptr);
    check_strmap(out, "abc=def;accept=text/*;accept-charset=utf-8;"
                 "from=foo;");

    settings.modes[HEADER_GROUP_CORS] = HEADER_FORWARD_YES;

    out = forward_request_headers(pool, headers,
                                  "192.168.0.2", "192.168.0.3",
                                  false, false, false, false,
                                  &settings,
                                  nullptr, nullptr, nullptr, nullptr);
    check_strmap(out, "abc=def;accept=text/*;accept-charset=utf-8;"
                 "access-control-request-method=POST;"
                 "from=foo;"
                 "origin=example.com;");

    /* forward secure headers */

    settings.modes[HEADER_GROUP_SECURE] = HEADER_FORWARD_YES;

    out = forward_request_headers(pool, headers,
                                  "192.168.0.2", "192.168.0.3",
                                  false, false, false, false,
                                  &settings,
                                  nullptr, nullptr, nullptr, nullptr);
    check_strmap(out, "abc=def;accept=text/*;accept-charset=utf-8;"
                 "access-control-request-method=POST;"
                 "from=foo;"
                 "origin=example.com;"
                 "x-cm4all-beng-user=hans;");

    /* response headers: nullptr */

    settings.modes[HEADER_GROUP_IDENTITY] = HEADER_FORWARD_NO;
    settings.modes[HEADER_GROUP_CAPABILITIES] = HEADER_FORWARD_NO;
    settings.modes[HEADER_GROUP_COOKIE] = HEADER_FORWARD_NO;
    settings.modes[HEADER_GROUP_OTHER] = HEADER_FORWARD_NO;
    settings.modes[HEADER_GROUP_FORWARD] = HEADER_FORWARD_NO;
    settings.modes[HEADER_GROUP_CORS] = HEADER_FORWARD_NO;
    settings.modes[HEADER_GROUP_SECURE] = HEADER_FORWARD_NO;

    out = forward_response_headers(pool, nullptr,
                                   "192.168.0.2", nullptr,
                                   &settings);
    assert(out->Remove("server") == nullptr);
    check_strmap(out, "");

    /* response headers: basic test */

    headers = strmap_new(pool);
    headers->Add("server", "apache");
    headers->Add("abc", "def");
    headers->Add("set-cookie", "a=b");
    headers->Add("content-type", "image/jpeg");
    headers->Add("via", "1.1 192.168.0.1");
    headers->Add("x-cm4all-beng-user", "hans");

    out = forward_response_headers(pool, headers,
                                   "192.168.0.2", nullptr,
                                   &settings);
    assert(strmap_get(out, "server") == nullptr);
    check_strmap(out, "content-type=image/jpeg;");

    /* response headers: server */

    settings.modes[HEADER_GROUP_CAPABILITIES] = HEADER_FORWARD_YES;

    out = forward_response_headers(pool, headers,
                                   "192.168.0.2", nullptr,
                                   &settings);
    check_strmap(out, "content-type=image/jpeg;server=apache;");

    /* response: forward via/x-forwarded-for as-is */

    settings.modes[HEADER_GROUP_IDENTITY] = HEADER_FORWARD_YES;

    out = forward_response_headers(pool, headers,
                                   "192.168.0.2", nullptr,
                                   &settings);
    check_strmap(out, "content-type=image/jpeg;server=apache;"
                 "via=1.1 192.168.0.1;");

    /* response: mangle via/x-forwarded-for */

    settings.modes[HEADER_GROUP_IDENTITY] = HEADER_FORWARD_MANGLE;

    out = forward_response_headers(pool, headers,
                                   "192.168.0.2", nullptr,
                                   &settings);
    check_strmap(out, "content-type=image/jpeg;server=apache;"
                 "via=1.1 192.168.0.1, 1.1 192.168.0.2;");

    settings.modes[HEADER_GROUP_IDENTITY] = HEADER_FORWARD_NO;

    /* forward cookies */

    settings.modes[HEADER_GROUP_COOKIE] = HEADER_FORWARD_YES;

    out = forward_response_headers(pool, headers,
                                   "192.168.0.2", nullptr,
                                   &settings);
    check_strmap(out, "content-type=image/jpeg;server=apache;"
                 "set-cookie=a=b;");

    /* forward CORS headers */

    headers->Add("access-control-allow-methods", "POST");

    out = forward_response_headers(pool, headers,
                                   "192.168.0.2", nullptr,
                                   &settings);
    check_strmap(out, "content-type=image/jpeg;server=apache;"
                 "set-cookie=a=b;");

    settings.modes[HEADER_GROUP_CORS] = HEADER_FORWARD_YES;

    out = forward_response_headers(pool, headers,
                                   "192.168.0.2", nullptr,
                                   &settings);
    check_strmap(out, "access-control-allow-methods=POST;"
                 "content-type=image/jpeg;server=apache;"
                 "set-cookie=a=b;");

    /* forward secure headers */

    settings.modes[HEADER_GROUP_SECURE] = HEADER_FORWARD_YES;

    out = forward_response_headers(pool, headers,
                                   "192.168.0.2", nullptr,
                                   &settings);
    check_strmap(out, "access-control-allow-methods=POST;"
                 "content-type=image/jpeg;server=apache;"
                 "set-cookie=a=b;"
                 "x-cm4all-beng-user=hans;");

    /* cleanup */

    tpool_deinit();
    pool_commit();

    pool_unref(pool);
    pool_commit();
    pool_recycler_clear();
}
