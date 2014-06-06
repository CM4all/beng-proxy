#include "header_forward.hxx"
#include "tpool.h"
#include "strmap.h"
#include "product.h"

#include <glib.h>

#include <assert.h>
#include <string.h>

static inline gint
cmp_pair(gconstpointer _a, gconstpointer _b)
{
    const strmap_pair *a = (strmap_pair *)_a;
    const strmap_pair *b = (strmap_pair *)_b;

    return strcmp(a->key, b->key);
}

static void
print_pair(gpointer data, gpointer user_data)
{
    const strmap_pair *pair = (strmap_pair *)data;
    char *p = (char *)user_data;

    strcat(p, pair->key);
    strcat(p, "=");
    strcat(p, pair->value);
    strcat(p, ";");
}

static const char *
strmap_to_string(struct strmap *map)
{
    union {
        const struct strmap_pair *pair;
        gpointer data;
    } u;
    GSList *list = nullptr;
    static char buffer[4096];

    strmap_rewind(map);
    while ((u.pair = strmap_next(map)) != nullptr)
        list = g_slist_prepend(list, u.data);

    list = g_slist_sort(list, cmp_pair);

    buffer[0] = 0;
    g_slist_foreach(list, print_pair, buffer);

    return buffer;
}

static void
check_strmap(struct strmap *map, const char *p)
{
    const char *q = strmap_to_string(map);

    assert(strcmp(q, p) == 0);
}

int main(G_GNUC_UNUSED int argc, G_GNUC_UNUSED char **argv)
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

    headers = strmap_new(pool, 17);
    strmap_add(headers, "from", "foo");
    strmap_add(headers, "abc", "def");
    strmap_add(headers, "cookie", "a=b");
    strmap_add(headers, "content-type", "image/jpeg");
    strmap_add(headers, "accept", "text/*");
    strmap_add(headers, "via", "1.1 192.168.0.1");
    strmap_add(headers, "x-forwarded-for", "10.0.0.2");
    strmap_add(headers, "x-cm4all-beng-user", "hans");

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
                                  nullptr, nullptr, nullptr);
    assert(strcmp(strmap_remove(out, "user-agent"), PRODUCT_TOKEN) == 0);
    check_strmap(out, "accept-charset=utf-8;"
                 "via=1.1 192.168.0.2;x-forwarded-for=192.168.0.3;");

    /* basic test */
    strmap_add(headers, "user-agent", "firesomething");
    out = forward_request_headers(pool, headers,
                                  "192.168.0.2", "192.168.0.3",
                                  false, false, false, false,
                                  &settings,
                                  nullptr, nullptr, nullptr);
    check_strmap(out, "accept=text/*;accept-charset=utf-8;"
                 "from=foo;user-agent=firesomething;"
                 "via=1.1 192.168.0.1, 1.1 192.168.0.2;"
                 "x-forwarded-for=10.0.0.2, 192.168.0.3;");

    /* no accept-charset forwarded */
    strmap_add(headers, "accept-charset", "iso-8859-1");

    out = forward_request_headers(pool, headers,
                                  "192.168.0.2", "192.168.0.3",
                                  false, false, false, false,
                                  &settings,
                                  nullptr, nullptr, nullptr);
    check_strmap(out, "accept=text/*;accept-charset=utf-8;"
                 "from=foo;user-agent=firesomething;"
                 "via=1.1 192.168.0.1, 1.1 192.168.0.2;"
                 "x-forwarded-for=10.0.0.2, 192.168.0.3;");

    /* now accept-charset is forwarded */
    out = forward_request_headers(pool, headers,
                                  "192.168.0.2", "192.168.0.3",
                                  false, false, true, false,
                                  &settings,
                                  nullptr, nullptr, nullptr);
    check_strmap(out, "accept=text/*;accept-charset=iso-8859-1;"
                 "from=foo;user-agent=firesomething;"
                 "via=1.1 192.168.0.1, 1.1 192.168.0.2;"
                 "x-forwarded-for=10.0.0.2, 192.168.0.3;");

    /* with request body */
    out = forward_request_headers(pool, headers,
                                  "192.168.0.2", "192.168.0.3",
                                  false, true, false, false,
                                  &settings,
                                  nullptr, nullptr, nullptr);
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
                                  nullptr, nullptr, nullptr);
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
                                  nullptr, nullptr, nullptr);
    assert(strcmp(strmap_remove(out, "user-agent"), PRODUCT_TOKEN) == 0);
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
                                  nullptr, nullptr, nullptr);
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
                                  nullptr, nullptr, nullptr);
    check_strmap(out, "accept=text/*;accept-charset=utf-8;"
                 "from=foo;");

    /* forward cookies */

    settings.modes[HEADER_GROUP_COOKIE] = HEADER_FORWARD_YES;

    out = forward_request_headers(pool, headers,
                                  "192.168.0.2", "192.168.0.3",
                                  false, false, false, false,
                                  &settings,
                                  nullptr, nullptr, nullptr);
    check_strmap(out, "accept=text/*;accept-charset=utf-8;"
                 "cookie=a=b;"
                 "from=foo;");

    /* forward 2 cookies */

    strmap_add(headers, "cookie", "c=d");

    out = forward_request_headers(pool, headers,
                                  "192.168.0.2", "192.168.0.3",
                                  false, false, false, false,
                                  &settings,
                                  nullptr, nullptr, nullptr);
    check_strmap(out, "accept=text/*;accept-charset=utf-8;"
                 "cookie=c=d;cookie=a=b;"
                 "from=foo;");

    /* forward other headers */

    settings.modes[HEADER_GROUP_COOKIE] = HEADER_FORWARD_NO;
    settings.modes[HEADER_GROUP_OTHER] = HEADER_FORWARD_YES;

    out = forward_request_headers(pool, headers,
                                  "192.168.0.2", "192.168.0.3",
                                  false, false, false, false,
                                  &settings,
                                  nullptr, nullptr, nullptr);
    check_strmap(out, "abc=def;accept=text/*;accept-charset=utf-8;"
                 "from=foo;");

    /* forward CORS headers */

    strmap_add(headers, "access-control-request-method", "POST");
    strmap_add(headers, "origin", "example.com");

    out = forward_request_headers(pool, headers,
                                  "192.168.0.2", "192.168.0.3",
                                  false, false, false, false,
                                  &settings,
                                  nullptr, nullptr, nullptr);
    check_strmap(out, "abc=def;accept=text/*;accept-charset=utf-8;"
                 "from=foo;");

    settings.modes[HEADER_GROUP_CORS] = HEADER_FORWARD_YES;

    out = forward_request_headers(pool, headers,
                                  "192.168.0.2", "192.168.0.3",
                                  false, false, false, false,
                                  &settings,
                                  nullptr, nullptr, nullptr);
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
                                  nullptr, nullptr, nullptr);
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
                                   "192.168.0.2",
                                   &settings);
    assert(strmap_remove(out, "server") == nullptr);
    check_strmap(out, "");

    /* response headers: basic test */

    headers = strmap_new(pool, 17);
    strmap_add(headers, "server", "apache");
    strmap_add(headers, "abc", "def");
    strmap_add(headers, "set-cookie", "a=b");
    strmap_add(headers, "content-type", "image/jpeg");
    strmap_add(headers, "via", "1.1 192.168.0.1");
    strmap_add(headers, "x-cm4all-beng-user", "hans");

    out = forward_response_headers(pool, headers,
                                   "192.168.0.2",
                                   &settings);
    assert(strmap_get(out, "server") == nullptr);
    check_strmap(out, "content-type=image/jpeg;");

    /* response headers: server */

    settings.modes[HEADER_GROUP_CAPABILITIES] = HEADER_FORWARD_YES;

    out = forward_response_headers(pool, headers,
                                   "192.168.0.2",
                                   &settings);
    check_strmap(out, "content-type=image/jpeg;server=apache;");

    /* response: forward via/x-forwarded-for as-is */

    settings.modes[HEADER_GROUP_IDENTITY] = HEADER_FORWARD_YES;

    out = forward_response_headers(pool, headers,
                                   "192.168.0.2",
                                   &settings);
    check_strmap(out, "content-type=image/jpeg;server=apache;"
                 "via=1.1 192.168.0.1;");

    /* response: mangle via/x-forwarded-for */

    settings.modes[HEADER_GROUP_IDENTITY] = HEADER_FORWARD_MANGLE;

    out = forward_response_headers(pool, headers,
                                   "192.168.0.2",
                                   &settings);
    check_strmap(out, "content-type=image/jpeg;server=apache;"
                 "via=1.1 192.168.0.1, 1.1 192.168.0.2;");

    settings.modes[HEADER_GROUP_IDENTITY] = HEADER_FORWARD_NO;

    /* forward cookies */

    settings.modes[HEADER_GROUP_COOKIE] = HEADER_FORWARD_YES;

    out = forward_response_headers(pool, headers,
                                   "192.168.0.2",
                                   &settings);
    check_strmap(out, "content-type=image/jpeg;server=apache;"
                 "set-cookie=a=b;");

    /* forward CORS headers */

    strmap_add(headers, "access-control-allow-methods", "POST");

    out = forward_response_headers(pool, headers,
                                   "192.168.0.2",
                                   &settings);
    check_strmap(out, "content-type=image/jpeg;server=apache;"
                 "set-cookie=a=b;");

    settings.modes[HEADER_GROUP_CORS] = HEADER_FORWARD_YES;

    out = forward_response_headers(pool, headers,
                                   "192.168.0.2",
                                   &settings);
    check_strmap(out, "access-control-allow-methods=POST;"
                 "content-type=image/jpeg;server=apache;"
                 "set-cookie=a=b;");

    /* forward secure headers */

    settings.modes[HEADER_GROUP_SECURE] = HEADER_FORWARD_YES;

    out = forward_response_headers(pool, headers,
                                   "192.168.0.2",
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
