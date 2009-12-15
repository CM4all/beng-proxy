/*
 * Which headers should be forwarded to/from remote HTTP servers?
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "header-forward.h"
#include "header-writer.h"
#include "strmap.h"
#include "session.h"
#include "cookie-client.h"
#include "growing-buffer.h"
#include "http.h"

#ifndef NDEBUG
#include <daemon/log.h>
#endif

#include <string.h>

static const char *const basic_request_headers[] = {
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

static const char *const cookie_request_headers[] = {
    "cookie",
    "cookie2",
    NULL,
};

/**
 * A list of request headers to be excluded from the "other" setting.
 */
static const char *const exclude_request_headers[] = {
    "accept-charset",
    "accept-encoding",
    "accept-language",
    "user-agent",
    "via",
    "x-forwarded-for",
    NULL,
};

static const char *const basic_response_headers[] = {
    "age",
    "etag",
    "cache-control",
    "content-encoding",
    "content-language",
    "content-md5",
    "content-range",
    "content-type",
    "content-disposition",
    "last-modified",
    "retry-after",
    "vary",
    "location",
    NULL,
};

static const char *const cookie_response_headers[] = {
    "set-cookie",
    "set-cookie2",
    NULL,
};

/**
 * A list of response headers to be excluded from the "other" setting.
 */
static const char *const exclude_response_headers[] = {
    "server",
    "via",
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

static void
forward_basic_headers(struct strmap *dest, const struct strmap *src,
                      bool with_body)
{
    headers_copy2(src, dest, basic_request_headers);
    if (with_body)
        headers_copy2(src, dest, body_request_headers);
}

static void
forward_user_agent(struct strmap *dest, const struct strmap *src,
                   bool mangle)
{
    const char *p;

    p = !mangle
        ? strmap_get_checked(src, "user-agent")
        : NULL;
    if (p == NULL)
        p = "beng-proxy v" VERSION;

    strmap_add(dest, "user-agent", p);
}

static void
forward_via(pool_t pool, struct strmap *dest, const struct strmap *src,
            const char *local_host, bool mangle)
{
    const char *p;

    p = strmap_get_checked(src, "via");
    if (p == NULL) {
        if (local_host != NULL && mangle)
            strmap_add(dest, "via",
                       p_strcat(pool, "1.1 ", local_host, NULL));
    } else {
        if (local_host == NULL || !mangle)
            strmap_add(dest, "via", p);
        else
            strmap_add(dest, "via",
                       p_strcat(pool, p, ", 1.1 ", local_host, NULL));
    }
}

static void
forward_xff(pool_t pool, struct strmap *dest, const struct strmap *src,
            const char *remote_host, bool mangle)
{
    const char *p;

    p = strmap_get_checked(src, "x-forwarded-for");
    if (p == NULL) {
        if (remote_host != NULL && mangle)
            strmap_add(dest, "x-forwarded-for", remote_host);
    } else {
        if (remote_host == NULL || !mangle)
            strmap_add(dest, "x-forwarded-for", p);
        else
            strmap_add(dest, "x-forwarded-for",
                       p_strcat(pool, p, ", ", remote_host, NULL));
    }
}

static void
forward_identity(pool_t pool, struct strmap *dest, const struct strmap *src,
                 const char *local_host, const char *remote_host,
                 bool mangle)
{
    forward_via(pool, dest, src, local_host, mangle);
    forward_xff(pool, dest, src, remote_host, mangle);
}

static bool
string_in_array(const char *const array[], const char *value)
{
    for (unsigned i = 0; array[i] != NULL; ++i)
        if (strcmp(array[i], value) == 0)
            return true;

    return false;
}

static void
forward_other_headers(struct strmap *dest, struct strmap *src)
{
    const struct strmap_pair *pair;

    strmap_rewind(src);
    while ((pair = strmap_next(src)) != NULL)
        if (!string_in_array(basic_request_headers, pair->key) &&
            !string_in_array(body_request_headers, pair->key) &&
            !string_in_array(language_request_headers, pair->key) &&
            !string_in_array(cookie_request_headers, pair->key) &&
            !string_in_array(exclude_request_headers, pair->key) &&
            memcmp(pair->key, "x-cm4all-beng-", 14) != 0 &&
            !http_header_is_hop_by_hop(pair->key))
            strmap_add(dest, pair->key, pair->value);
}

struct strmap *
forward_request_headers(pool_t pool, struct strmap *src,
                        const char *local_host, const char *remote_host,
                        bool with_body, bool forward_charset,
                        bool forward_encoding,
                        const struct header_forward_settings *settings,
                        const struct session *session,
                        const char *host_and_port, const char *uri)
{
    struct strmap *dest;
    const char *p;

    assert(settings != NULL);

#ifndef NDEBUG
    if (session != NULL && daemon_log_config.verbose >= 10) {
        struct session_id_string s;
        daemon_log(10, "forward_request_headers remote_host='%s' "
                   "host='%s' uri='%s' session=%s user='%s' cookie='%s'\n",
                   remote_host, host_and_port, uri,
                   session_id_format(session->id, &s),
                   session->user,
                   host_and_port != NULL && uri != NULL
                   ? cookie_jar_http_header_value(session->cookies,
                                                  host_and_port, uri, pool)
                   : NULL);
    }
#endif

    dest = strmap_new(pool, 32);

    if (src != NULL)
        forward_basic_headers(dest, src, with_body);

    if (src != NULL &&
        settings->modes[HEADER_GROUP_OTHER] == HEADER_FORWARD_YES)
        forward_other_headers(dest, src);

    p = forward_charset
        ? strmap_get_checked(src, "accept-charset")
        : NULL;
    if (p == NULL)
        p = "utf-8";
    strmap_add(dest, "accept-charset", p);

    if (forward_encoding &&
        (p = strmap_get_checked(src, "accept-encoding")) != NULL)
        strmap_add(dest, "accept-encoding", p);

    if (settings->modes[HEADER_GROUP_COOKIE] == HEADER_FORWARD_YES) {
        if (src != NULL)
            headers_copy2(src, dest, cookie_request_headers);
    } else if (settings->modes[HEADER_GROUP_COOKIE] == HEADER_FORWARD_MANGLE &&
               session != NULL && host_and_port != NULL && uri != NULL)
        cookie_jar_http_header(session->cookies, host_and_port, uri,
                               dest, pool);

    if (session != NULL && session->language != NULL)
        strmap_add(dest, "accept-language", p_strdup(pool, session->language));
    else if (src != NULL)
        headers_copy2(src, dest, language_request_headers);

    if (session != NULL && session->user != NULL)
        strmap_add(dest, "x-cm4all-beng-user", p_strdup(pool, session->user));

    if (settings->modes[HEADER_GROUP_CAPABILITIES] != HEADER_FORWARD_NO)
        forward_user_agent(dest, src,
                           settings->modes[HEADER_GROUP_CAPABILITIES] == HEADER_FORWARD_MANGLE);

    if (settings->modes[HEADER_GROUP_IDENTITY] != HEADER_FORWARD_NO)
        forward_identity(pool, dest, src, local_host, remote_host,
                         settings->modes[HEADER_GROUP_IDENTITY] == HEADER_FORWARD_MANGLE);

    return dest;
}

static void
forward_other_response_headers(struct strmap *dest, struct strmap *src)
{
    const struct strmap_pair *pair;

    strmap_rewind(src);
    while ((pair = strmap_next(src)) != NULL)
        if (!string_in_array(basic_response_headers, pair->key) &&
            !string_in_array(cookie_response_headers, pair->key) &&
            !string_in_array(exclude_response_headers, pair->key) &&
            memcmp(pair->key, "x-cm4all-beng-", 14) != 0 &&
            !http_header_is_hop_by_hop(pair->key))
            strmap_add(dest, pair->key, pair->value);
}

static void
forward_server(struct strmap *dest, const struct strmap *src,
               bool mangle)
{
    const char *p;

    p = !mangle
        ? strmap_get_checked(src, "server")
        : NULL;
    if (p == NULL)
        p = "beng-proxy v" VERSION;

    strmap_add(dest, "server", p);
}

struct strmap *
forward_response_headers(pool_t pool, struct strmap *src,
                         const char *local_host,
                         const struct header_forward_settings *settings)
{
    struct strmap *dest;

    assert(settings != NULL);

    dest = strmap_new(pool, 61);
    if (src != NULL) {
        headers_copy2(src, dest, basic_response_headers);

        if (settings->modes[HEADER_GROUP_OTHER] == HEADER_FORWARD_YES)
            forward_other_response_headers(dest, src);

        if (settings->modes[HEADER_GROUP_COOKIE] == HEADER_FORWARD_YES)
            headers_copy2(src, dest, cookie_response_headers);
    }

    /* RFC 2616 3.8: Product Tokens */
    forward_server(dest, src,
                   settings->modes[HEADER_GROUP_CAPABILITIES] != HEADER_FORWARD_YES);

    if (settings->modes[HEADER_GROUP_IDENTITY] != HEADER_FORWARD_NO)
        forward_via(pool, dest, src, local_host,
                    settings->modes[HEADER_GROUP_IDENTITY] == HEADER_FORWARD_MANGLE);

    return dest;
}
