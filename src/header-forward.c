/*
 * Which headers should be forwarded to/from remote HTTP servers?
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "header-forward.h"
#include "strmap.h"
#include "session.h"
#include "cookie-client.h"

static const char *const request_headers[] = {
    "accept",
    "from",
    "cache-control",
    NULL,
};

static const char *const language_request_headers[] = {
    "accept-language",
    NULL,
};

static const char *const body_request_headers[] = {
    "content-encoding",
    "content-language",
    "content-md5",
    "content-range",
    "content-type",
    "content-disposition",
    NULL,
};

static void
headers_copy2(const struct strmap *in, struct strmap *out,
              const char *const* keys)
{
    const char *value;

    for (; *keys != NULL; ++keys) {
        value = strmap_get(in, *keys);
        if (value != NULL)
            strmap_set(out, *keys, value);
    }
}

struct strmap *
forward_request_headers(pool_t pool, struct strmap *src,
                        const char *remote_host,
                        bool with_body,
                        const struct session *session,
                        const char *host_and_port, const char *uri)
{
    struct strmap *dest;
    const char *p;

    dest = strmap_new(pool, 32);
    strmap_add(dest, "accept-charset", "utf-8");

    if (src != NULL) {
        headers_copy2(src, dest, request_headers);
        if (with_body)
            headers_copy2(src, dest, body_request_headers);
    }

    if (session != NULL && host_and_port != NULL && uri != NULL)
        cookie_jar_http_header(session->cookies, host_and_port, uri,
                               dest, pool);

    if (session != NULL && session->language != NULL)
        strmap_add(dest, "accept-language", p_strdup(pool, session->language));
    else if (src != NULL)
        headers_copy2(src, dest, language_request_headers);

    if (session != NULL && session->user != NULL)
        strmap_add(dest, "x-cm4all-beng-user", p_strdup(pool, session->user));

    p = strmap_get_checked(src, "user-agent");
    if (p == NULL)
        p = "beng-proxy v" VERSION;
    strmap_add(dest, "user-agent", p);

    p = strmap_get_checked(src, "via");
    if (p == NULL) {
        if (remote_host != NULL)
            strmap_add(dest, "via",
                       p_strcat(pool, "1.1 ", remote_host, NULL));
    } else {
        if (remote_host == NULL)
            strmap_add(dest, "via", p);
        else
            strmap_add(dest, "via",
                       p_strcat(pool, p, ", 1.1 ", remote_host, NULL));
    }

    return dest;
}
