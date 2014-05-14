/*
 * Which headers should be forwarded to/from remote HTTP servers?
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "header_forward.hxx"
#include "header_copy.hxx"
#include "header-writer.h"
#include "strmap.h"
#include "session.hxx"
#include "cookie_client.hxx"
#include "growing-buffer.h"
#include "pool.h"
#include "product.h"

#ifndef NDEBUG
#include <daemon/log.h>
#endif

#include <http/header.h>

#include <string.h>

static const char *const basic_request_headers[] = {
    "accept",
    "from",
    "cache-control",
    nullptr,
};

static const char *const language_request_headers[] = {
    "accept-language",
    nullptr,
};

static const char *const body_request_headers[] = {
    "content-encoding",
    "content-language",
    "content-md5",
    "content-range",
    "content-type",
    "content-disposition",
    nullptr,
};

static const char *const cookie_request_headers[] = {
    "cookie",
    "cookie2",
    nullptr,
};

/**
 * @see http://www.w3.org/TR/cors/#syntax
 */
static const char *const cors_request_headers[] = {
    "origin",
    "access-control-request-method",
    "access-control-request-headers",
    nullptr,
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
    "host",
    nullptr,
};

static const char *const basic_response_headers[] = {
    "age",
    "etag",
    "cache-control",
    "expires",
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
    nullptr,
};

static const char *const cookie_response_headers[] = {
    "set-cookie",
    "set-cookie2",
    nullptr,
};

/**
 * @see http://www.w3.org/TR/cors/#syntax
 */
static const char *const cors_response_headers[] = {
    "access-control-allow-origin",
    "access-control-allow-credentials",
    "access-control-expose-headers",
    "access-control-max-age",
    "access-control-allow-methods",
    "access-control-allow-headers",
    nullptr,
};

/**
 * A list of response headers to be excluded from the "other" setting.
 */
static const char *const exclude_response_headers[] = {
    "server",
    "via",
    "date",
    nullptr,
};

/**
 * @see #HEADER_GROUP_SECURE
 */
gcc_pure
static bool
is_secure_header(const char *name)
{
    return memcmp(name, "x-cm4all-beng-", 14) == 0;
}

static void
forward_basic_headers(struct strmap *dest, const struct strmap *src,
                      bool with_body)
{
    header_copy_list(src, dest, basic_request_headers);
    if (with_body)
        header_copy_list(src, dest, body_request_headers);
}

static void
forward_secure_headers(struct strmap *dest, struct strmap *src)
{
    strmap_rewind(src);

    const struct strmap_pair *pair;
    while ((pair = strmap_next(src)) != nullptr)
        if (is_secure_header(pair->key))
            strmap_add(dest, pair->key, pair->value);
}

static void
forward_user_agent(struct strmap *dest, const struct strmap *src,
                   bool mangle)
{
    const char *p;

    p = !mangle
        ? strmap_get_checked(src, "user-agent")
        : nullptr;
    if (p == nullptr)
        p = PRODUCT_TOKEN;

    strmap_add(dest, "user-agent", p);
}

static void
forward_via(struct pool *pool, struct strmap *dest, const struct strmap *src,
            const char *local_host, bool mangle)
{
    const char *p;

    p = strmap_get_checked(src, "via");
    if (p == nullptr) {
        if (local_host != nullptr && mangle)
            strmap_add(dest, "via",
                       p_strcat(pool, "1.1 ", local_host, nullptr));
    } else {
        if (local_host == nullptr || !mangle)
            strmap_add(dest, "via", p);
        else
            strmap_add(dest, "via",
                       p_strcat(pool, p, ", 1.1 ", local_host, nullptr));
    }
}

static void
forward_xff(struct pool *pool, struct strmap *dest, const struct strmap *src,
            const char *remote_host, bool mangle)
{
    const char *p;

    p = strmap_get_checked(src, "x-forwarded-for");
    if (p == nullptr) {
        if (remote_host != nullptr && mangle)
            strmap_add(dest, "x-forwarded-for", remote_host);
    } else {
        if (remote_host == nullptr || !mangle)
            strmap_add(dest, "x-forwarded-for", p);
        else
            strmap_add(dest, "x-forwarded-for",
                       p_strcat(pool, p, ", ", remote_host, nullptr));
    }
}

static void
forward_identity(struct pool *pool, struct strmap *dest, const struct strmap *src,
                 const char *local_host, const char *remote_host,
                 bool mangle)
{
    forward_via(pool, dest, src, local_host, mangle);
    forward_xff(pool, dest, src, remote_host, mangle);
}

static bool
string_in_array(const char *const array[], const char *value)
{
    for (unsigned i = 0; array[i] != nullptr; ++i)
        if (strcmp(array[i], value) == 0)
            return true;

    return false;
}

static void
forward_other_headers(struct strmap *dest, struct strmap *src)
{
    const struct strmap_pair *pair;

    strmap_rewind(src);
    while ((pair = strmap_next(src)) != nullptr)
        if (!string_in_array(basic_request_headers, pair->key) &&
            !string_in_array(body_request_headers, pair->key) &&
            !string_in_array(language_request_headers, pair->key) &&
            !string_in_array(cookie_request_headers, pair->key) &&
            !string_in_array(cors_request_headers, pair->key) &&
            !string_in_array(exclude_request_headers, pair->key) &&
            !is_secure_header(pair->key) &&
            !http_header_is_hop_by_hop(pair->key))
            strmap_add(dest, pair->key, pair->value);
}

struct strmap *
forward_request_headers(struct pool *pool, struct strmap *src,
                        const char *local_host, const char *remote_host,
                        bool exclude_host,
                        bool with_body, bool forward_charset,
                        bool forward_encoding,
                        const struct header_forward_settings *settings,
                        const struct session *session,
                        const char *host_and_port, const char *uri)
{
    struct strmap *dest;
    const char *p;

    assert(settings != nullptr);

#ifndef NDEBUG
    if (session != nullptr && daemon_log_config.verbose >= 10) {
        struct session_id_string s;
        daemon_log(10, "forward_request_headers remote_host='%s' "
                   "host='%s' uri='%s' session=%s user='%s' cookie='%s'\n",
                   remote_host, host_and_port, uri,
                   session_id_format(session->id, &s),
                   session->user,
                   host_and_port != nullptr && uri != nullptr
                   ? cookie_jar_http_header_value(session->cookies,
                                                  host_and_port, uri, pool)
                   : nullptr);
    }
#endif

    dest = strmap_new(pool, 32);

    if (src != nullptr) {
        forward_basic_headers(dest, src, with_body);

        if (!exclude_host)
            header_copy_one(src, dest, "host");

        if (settings->modes[HEADER_GROUP_CORS] == HEADER_FORWARD_YES)
            header_copy_list(src, dest, cors_request_headers);

        if (settings->modes[HEADER_GROUP_SECURE] == HEADER_FORWARD_YES)
            forward_secure_headers(dest, src);

        if (settings->modes[HEADER_GROUP_OTHER] == HEADER_FORWARD_YES)
            forward_other_headers(dest, src);
    }

    p = forward_charset
        ? strmap_get_checked(src, "accept-charset")
        : nullptr;
    if (p == nullptr)
        p = "utf-8";
    strmap_add(dest, "accept-charset", p);

    if (forward_encoding &&
        (p = strmap_get_checked(src, "accept-encoding")) != nullptr)
        strmap_add(dest, "accept-encoding", p);

    if (settings->modes[HEADER_GROUP_COOKIE] == HEADER_FORWARD_YES) {
        if (src != nullptr)
            header_copy_list(src, dest, cookie_request_headers);
    } else if (settings->modes[HEADER_GROUP_COOKIE] == HEADER_FORWARD_MANGLE &&
               session != nullptr && host_and_port != nullptr && uri != nullptr)
        cookie_jar_http_header(session->cookies, host_and_port, uri,
                               dest, pool);

    if (session != nullptr && session->language != nullptr)
        strmap_add(dest, "accept-language", p_strdup(pool, session->language));
    else if (src != nullptr)
        header_copy_list(src, dest, language_request_headers);

    if (session != nullptr && session->user != nullptr)
        strmap_add(dest, "x-cm4all-beng-user", p_strdup(pool, session->user));

    if (settings->modes[HEADER_GROUP_CAPABILITIES] != HEADER_FORWARD_NO)
        forward_user_agent(dest, src,
                           settings->modes[HEADER_GROUP_CAPABILITIES] == HEADER_FORWARD_MANGLE);

    if (settings->modes[HEADER_GROUP_IDENTITY] != HEADER_FORWARD_NO)
        forward_identity(pool, dest, src, local_host, remote_host,
                         settings->modes[HEADER_GROUP_IDENTITY] == HEADER_FORWARD_MANGLE);

    if (settings->modes[HEADER_GROUP_FORWARD] == HEADER_FORWARD_MANGLE) {
        const char *host = strmap_get_checked(src, "host");
        if (host != nullptr)
            strmap_add(dest, "x-forwarded-host", host);
    }

    return dest;
}

static void
forward_other_response_headers(struct strmap *dest, struct strmap *src)
{
    const struct strmap_pair *pair;

    strmap_rewind(src);
    while ((pair = strmap_next(src)) != nullptr)
        if (!string_in_array(basic_response_headers, pair->key) &&
            !string_in_array(cookie_response_headers, pair->key) &&
            !string_in_array(cors_response_headers, pair->key) &&
            !string_in_array(exclude_response_headers, pair->key) &&
            !is_secure_header(pair->key) &&
            !http_header_is_hop_by_hop(pair->key))
            strmap_add(dest, pair->key, pair->value);
}

static void
forward_server(struct strmap *dest, const struct strmap *src,
               bool mangle)
{
    const char *p;

    if (mangle)
        return;

    p = strmap_get_checked(src, "server");
    if (p == nullptr)
        return;

    strmap_add(dest, "server", p);
}

struct strmap *
forward_response_headers(struct pool *pool, struct strmap *src,
                         const char *local_host,
                         const struct header_forward_settings *settings)
{
    struct strmap *dest;

    assert(settings != nullptr);

    dest = strmap_new(pool, 61);
    if (src != nullptr) {
        header_copy_list(src, dest, basic_response_headers);

        if (settings->modes[HEADER_GROUP_OTHER] == HEADER_FORWARD_YES)
            forward_other_response_headers(dest, src);

        if (settings->modes[HEADER_GROUP_COOKIE] == HEADER_FORWARD_YES)
            header_copy_list(src, dest, cookie_response_headers);

        if (settings->modes[HEADER_GROUP_CORS] == HEADER_FORWARD_YES)
            header_copy_list(src, dest, cors_response_headers);

        if (settings->modes[HEADER_GROUP_SECURE] == HEADER_FORWARD_YES)
            forward_secure_headers(dest, src);
    }

    /* RFC 2616 3.8: Product Tokens */
    forward_server(dest, src,
                   settings->modes[HEADER_GROUP_CAPABILITIES] != HEADER_FORWARD_YES);

    if (settings->modes[HEADER_GROUP_IDENTITY] != HEADER_FORWARD_NO)
        forward_via(pool, dest, src, local_host,
                    settings->modes[HEADER_GROUP_IDENTITY] == HEADER_FORWARD_MANGLE);

    return dest;
}
