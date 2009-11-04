#include "header-forward.h"
#include "tpool.h"
#include "strmap.h"

#include <glib.h>

#include <assert.h>
#include <string.h>

static inline gint
cmp_pair(gconstpointer _a, gconstpointer _b)
{
    const struct strmap_pair *a = _a;
    const struct strmap_pair *b = _b;

    return strcmp(a->key, b->key);
}

static void
print_pair(gpointer data, gpointer user_data)
{
    const struct strmap_pair *pair = data;
    char *p = user_data;

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
    GSList *list = NULL;
    static char buffer[4096];

    strmap_rewind(map);
    while ((u.pair = strmap_next(map)) != NULL)
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
    pool_t pool;
    struct strmap *headers, *out;

    pool = pool_new_libc(NULL, "root");
    tpool_init(pool);

    headers = strmap_new(pool, 17);
    strmap_add(headers, "from", "foo");
    strmap_add(headers, "abc", "def");
    strmap_add(headers, "cookie", "a=b");
    strmap_add(headers, "content-type", "image/jpeg");
    strmap_add(headers, "accept", "text/*");
    strmap_add(headers, "via", "1.1 192.168.0.1");
    strmap_add(headers, "x-forwarded-for", "10.0.0.2");

    /* verify strmap_to_string() */
    check_strmap(headers, "abc=def;accept=text/*;"
                 "content-type=image/jpeg;cookie=a=b;from=foo;"
                 "via=1.1 192.168.0.1;x-forwarded-for=10.0.0.2;");

    /* NULL test */
    out = forward_request_headers(pool, NULL,
                                  "192.168.0.2", "192.168.0.3",
                                  false, false,
                                  NULL, NULL, NULL);
    assert(g_str_has_prefix(strmap_remove(out, "user-agent"), "beng-proxy"));
    check_strmap(out, "accept-charset=utf-8;"
                 "via=1.1 192.168.0.2;x-forwarded-for=192.168.0.3;");

    /* basic test */
    strmap_add(headers, "user-agent", "firesomething");
    out = forward_request_headers(pool, headers,
                                  "192.168.0.2", "192.168.0.3",
                                  false, false,
                                  NULL, NULL, NULL);
    check_strmap(out, "accept=text/*;accept-charset=utf-8;"
                 "from=foo;user-agent=firesomething;"
                 "via=1.1 192.168.0.1, 1.1 192.168.0.2;"
                 "x-forwarded-for=10.0.0.2, 192.168.0.3;");

    /* no accept-charset forwarded */
    strmap_add(headers, "accept-charset", "iso-8859-1");

    out = forward_request_headers(pool, headers,
                                  "192.168.0.2", "192.168.0.3",
                                  false, false,
                                  NULL, NULL, NULL);
    check_strmap(out, "accept=text/*;accept-charset=utf-8;"
                 "from=foo;user-agent=firesomething;"
                 "via=1.1 192.168.0.1, 1.1 192.168.0.2;"
                 "x-forwarded-for=10.0.0.2, 192.168.0.3;");

    /* now accept-charset is forwarded */
    out = forward_request_headers(pool, headers,
                                  "192.168.0.2", "192.168.0.3",
                                  false, true,
                                  NULL, NULL, NULL);
    check_strmap(out, "accept=text/*;accept-charset=iso-8859-1;"
                 "from=foo;user-agent=firesomething;"
                 "via=1.1 192.168.0.1, 1.1 192.168.0.2;"
                 "x-forwarded-for=10.0.0.2, 192.168.0.3;");

    /* with request body */
    out = forward_request_headers(pool, headers,
                                  "192.168.0.2", "192.168.0.3",
                                  true, false,
                                  NULL, NULL, NULL);
    check_strmap(out, "accept=text/*;accept-charset=utf-8;"
                 "content-type=image/jpeg;from=foo;"
                 "user-agent=firesomething;"
                 "via=1.1 192.168.0.1, 1.1 192.168.0.2;"
                 "x-forwarded-for=10.0.0.2, 192.168.0.3;");

    /* cleanup */

    tpool_deinit();
    pool_commit();

    pool_unref(pool);
    pool_commit();
    pool_recycler_clear();
}
