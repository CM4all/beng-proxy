/*
 * Which headers should be forwarded to/from remote HTTP servers?
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "header_forward.hxx"
#include "header_copy.hxx"
#include "header_writer.hxx"
#include "http_upgrade.hxx"
#include "strmap.hxx"
#include "session.hxx"
#include "cookie_client.hxx"
#include "cookie_server.hxx"
#include "pool.hxx"
#include "product.h"
#include "util/CharUtil.hxx"
#include "util/StringCompare.hxx"

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

static const char *const cache_request_headers[] = {
    "if-modified-since",
    "if-unmodified-since",
    "if-match",
    "if-none-match",
    "if-range",
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
 * A list of response headers to for the "ssl" setting.
 */
static const char *const ssl_request_headers[] = {
    "x-cm4all-beng-peer-subject",
    "x-cm4all-beng-peer-issuer-subject",
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
    "allow",
    "etag",
    "cache-control",
    "expires",
    "content-encoding",
    "content-language",
    "content-md5",
    "content-range",
    "accept-ranges",
    "content-type",
    "content-disposition",
    "last-modified",
    "retry-after",
    "vary",
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

gcc_pure
static bool
string_in_array(const char *const array[], const char *value)
{
    for (unsigned i = 0; array[i] != nullptr; ++i)
        if (strcmp(array[i], value) == 0)
            return true;

    return false;
}

static void
forward_upgrade_request_headers(StringMap &dest, const StringMap &src,
                                bool with_body)
{
    if (with_body && http_is_upgrade(src))
        header_copy_list(src, dest, http_upgrade_request_headers);
}

static void
forward_upgrade_response_headers(StringMap &dest, http_status_t status,
                                 const StringMap &src)
{
    if (http_is_upgrade(status, src))
        header_copy_list(src, dest, http_upgrade_response_headers);
}

/**
 * @see #HEADER_GROUP_SSL
 */
gcc_pure
static bool
is_ssl_header(const char *name)
{
    return string_in_array(ssl_request_headers, name);
}

/**
 * @see #HEADER_GROUP_SECURE
 */
gcc_pure
static bool
is_secure_header(const char *name)
{
    return StringStartsWith(name, "x-cm4all-beng-") && !is_ssl_header(name);
}

gcc_pure
static bool
is_secure_or_ssl_header(const char *name)
{
    return StringStartsWith(name, "x-cm4all-beng-");
}

/**
 * @see #HEADER_GROUP_TRANSFORMATION
 */
gcc_pure
static bool
is_transformation_header(const char *name)
{
    return StringStartsWith(name, "x-cm4all-view");
}

static void
forward_basic_headers(StringMap &dest, const StringMap &src,
                      bool with_body)
{
    header_copy_list(src, dest, basic_request_headers);
    if (with_body)
        header_copy_list(src, dest, body_request_headers);
}

static void
forward_secure_headers(StringMap &dest, const StringMap &src)
{
    for (const auto &i : src)
        if (is_secure_header(i.key))
            dest.Add(i.key, i.value);
}

static void
forward_ssl_headers(StringMap &dest, const StringMap &src)
{
    for (const auto &i : src)
        if (is_ssl_header(i.key))
            dest.Add(i.key, i.value);
}

static void
forward_transformation_headers(StringMap &dest, const StringMap &src)
{
    header_copy_one(src, dest, "x-cm4all-view");
}

/**
 * @see #HEADER_GROUP_LINK
 */
gcc_pure
static bool
IsLinkRequestHeader(const char *name)
{
    return strcmp(name, "referer") == 0;
}

static void
ForwardLinkRequestHeaders(StringMap &dest, const StringMap &src)
{
    header_copy_one(src, dest, "referer");
}

/**
 * @see #HEADER_GROUP_LINK
 */
gcc_pure
static bool
IsLinkResponseHeader(const char *name)
{
    return strcmp(name, "location") == 0;
}

static void
forward_link_response_headers(StringMap &dest, const StringMap &src,
                              const char *(*relocate)(const char *uri,
                                                      void *ctx),
                              void *relocate_ctx,
                              enum beng_header_forward_mode mode)
{
    if (mode == HEADER_FORWARD_YES)
        header_copy_one(src, dest, "location");
    else if (mode == HEADER_FORWARD_MANGLE) {
        const char *location = src.Get("location");
        if (location != nullptr) {
            const char *new_location = relocate != nullptr
                ? relocate(location, relocate_ctx)
                : location;
            if (new_location != nullptr)
                dest.Add("location", new_location);
        }
    }
}

static void
forward_user_agent(StringMap &dest, const StringMap &src,
                   bool mangle)
{
    const char *p;

    p = !mangle
        ? src.Get("user-agent")
        : nullptr;
    if (p == nullptr)
        p = PRODUCT_TOKEN;

    dest.Add("user-agent", p);
}

static void
forward_via(struct pool &pool, StringMap &dest, const StringMap &src,
            const char *local_host, bool mangle)
{
    const char *p = src.Get("via");
    if (p == nullptr) {
        if (local_host != nullptr && mangle)
            dest.Add("via", p_strcat(&pool, "1.1 ", local_host, nullptr));
    } else {
        if (local_host == nullptr || !mangle)
            dest.Add("via", p);
        else
            dest.Add("via", p_strcat(&pool, p, ", 1.1 ", local_host, nullptr));
    }
}

static void
forward_xff(struct pool &pool, StringMap &dest, const StringMap &src,
            const char *remote_host, bool mangle)
{
    const char *p;

    p = src.Get("x-forwarded-for");
    if (p == nullptr) {
        if (remote_host != nullptr && mangle)
            dest.Add("x-forwarded-for", remote_host);
    } else {
        if (remote_host == nullptr || !mangle)
            dest.Add("x-forwarded-for", p);
        else
            dest.Add("x-forwarded-for",
                     p_strcat(&pool, p, ", ", remote_host, nullptr));
    }
}

static void
forward_identity(struct pool &pool,
                 StringMap &dest, const StringMap &src,
                 const char *local_host, const char *remote_host,
                 bool mangle)
{
    forward_via(pool, dest, src, local_host, mangle);
    forward_xff(pool, dest, src, remote_host, mangle);
}

static void
forward_other_headers(StringMap &dest, const StringMap &src)
{
    for (const auto &i : src)
        if (!string_in_array(basic_request_headers, i.key) &&
            !string_in_array(body_request_headers, i.key) &&
            !string_in_array(language_request_headers, i.key) &&
            !string_in_array(cookie_request_headers, i.key) &&
            !string_in_array(cors_request_headers, i.key) &&
            !string_in_array(cache_request_headers, i.key) &&
            !string_in_array(exclude_request_headers, i.key) &&
            !is_secure_or_ssl_header(i.key) &&
            !IsLinkRequestHeader(i.key) &&
            strcmp(i.key, "range") != 0 &&
            !http_header_is_hop_by_hop(i.key))
            dest.Add(i.key, i.value);
}

/**
 * Copy cookie request headers, but exclude one cookie name.
 */
static void
header_copy_cookie_except(struct pool &pool,
                          StringMap &dest, const StringMap &src,
                          const char *except)
{
    for (const auto &i : src) {
        if (strcmp(i.key, "cookie2") == 0)
            dest.Add(i.key, i.value);
        else if (strcmp(i.key, "cookie") == 0) {
            const char *new_value = cookie_exclude(i.value, except, &pool);
            if (new_value != nullptr)
                dest.Add(i.key, new_value);
        }
    }
}

gcc_pure
static bool
compare_set_cookie_name(const char *set_cookie, const char *name)
{
    auto suffix = StringAfterPrefix(set_cookie, name);
    return suffix != nullptr && !IsAlphaNumericASCII(*suffix);
}

/**
 * Copy cookie response headers, but exclude one cookie name.
 */
static void
header_copy_set_cookie_except(StringMap &dest, const StringMap &src,
                              const char *except)
{
    for (const auto &i : src)
        if (string_in_array(cookie_response_headers, i.key) &&
            !compare_set_cookie_name(i.value, except))
            dest.Add(i.key, i.value);
}

StringMap
forward_request_headers(struct pool &pool, const StringMap &src,
                        const char *local_host, const char *remote_host,
                        bool exclude_host,
                        bool with_body, bool forward_charset,
                        bool forward_encoding,
                        bool forward_range,
                        const struct header_forward_settings &settings,
                        const char *session_cookie,
                        const RealmSession *session,
                        const char *host_and_port, const char *uri)
{
    const char *p;

#ifndef NDEBUG
    if (session != nullptr && daemon_log_config.verbose >= 10) {
        struct session_id_string s;
        daemon_log(10, "forward_request_headers remote_host='%s' "
                   "host='%s' uri='%s' session=%s user='%s' cookie='%s'\n",
                   remote_host, host_and_port, uri,
                   session->parent.id.Format(s),
                   session->user.c_str(),
                   host_and_port != nullptr && uri != nullptr
                   ? cookie_jar_http_header_value(session->cookies,
                                                  host_and_port, uri, pool)
                   : nullptr);
    }
#endif

    StringMap dest(pool);

    forward_basic_headers(dest, src, with_body);
    forward_upgrade_request_headers(dest, src, with_body);

    if (!exclude_host)
        header_copy_one(src, dest, "host");

    if (settings.modes[HEADER_GROUP_CORS] == HEADER_FORWARD_YES)
        header_copy_list(src, dest, cors_request_headers);

    if (settings.modes[HEADER_GROUP_SECURE] == HEADER_FORWARD_YES)
        forward_secure_headers(dest, src);

    if (settings.modes[HEADER_GROUP_SSL] == HEADER_FORWARD_YES)
        forward_ssl_headers(dest, src);

    if (settings.modes[HEADER_GROUP_LINK] == HEADER_FORWARD_YES)
        ForwardLinkRequestHeaders(dest, src);

    if (settings.modes[HEADER_GROUP_OTHER] == HEADER_FORWARD_YES)
        forward_other_headers(dest, src);

    p = forward_charset
        ? src.Get("accept-charset")
        : nullptr;
    if (p == nullptr)
        p = "utf-8";
    dest.Add("accept-charset", p);

    if (forward_encoding &&
        (p = src.Get("accept-encoding")) != nullptr)
        dest.Add("accept-encoding", p);

    if (forward_range) {
        p = src.Get("range");
        if (p != nullptr)
            dest.Add("range", p);

        // TODO: separate parameter for cache headers
        header_copy_list(src, dest, cache_request_headers);
    }

    if (settings.modes[HEADER_GROUP_COOKIE] == HEADER_FORWARD_YES) {
        header_copy_list(src, dest, cookie_request_headers);
    } else if (settings.modes[HEADER_GROUP_COOKIE] == HEADER_FORWARD_BOTH) {
        if (session_cookie == nullptr)
            header_copy_list(src, dest, cookie_request_headers);
        else
            header_copy_cookie_except(pool, dest, src, session_cookie);
    } else if (settings.modes[HEADER_GROUP_COOKIE] == HEADER_FORWARD_MANGLE &&
               session != nullptr && host_and_port != nullptr && uri != nullptr)
        cookie_jar_http_header(session->cookies, host_and_port, uri,
                               dest, pool);

    if (session != nullptr && session->parent.language != nullptr)
        dest.Add("accept-language",
                  p_strdup(&pool, session->parent.language));
    else
        header_copy_list(src, dest, language_request_headers);

    if (session != nullptr && session->user != nullptr)
        dest.Add("x-cm4all-beng-user", p_strdup(&pool, session->user));

    if (settings.modes[HEADER_GROUP_CAPABILITIES] != HEADER_FORWARD_NO)
        forward_user_agent(dest, src,
                           settings.modes[HEADER_GROUP_CAPABILITIES] == HEADER_FORWARD_MANGLE);

    if (settings.modes[HEADER_GROUP_IDENTITY] != HEADER_FORWARD_NO)
        forward_identity(pool, dest, src, local_host, remote_host,
                         settings.modes[HEADER_GROUP_IDENTITY] == HEADER_FORWARD_MANGLE);

    if (settings.modes[HEADER_GROUP_FORWARD] == HEADER_FORWARD_MANGLE) {
        const char *host = src.Get("host");
        if (host != nullptr)
            dest.Add("x-forwarded-host", host);
    }

    return dest;
}

static void
forward_other_response_headers(StringMap &dest, const StringMap &src)
{
    for (const auto &i : src)
        if (!string_in_array(basic_response_headers, i.key) &&
            !string_in_array(cookie_response_headers, i.key) &&
            !string_in_array(cors_response_headers, i.key) &&
            !string_in_array(exclude_response_headers, i.key) &&
            !IsLinkResponseHeader(i.key) &&
            !is_secure_or_ssl_header(i.key) &&
            !is_transformation_header(i.key) &&
            !http_header_is_hop_by_hop(i.key))
            dest.Add(i.key, i.value);
}

static void
forward_server(StringMap &dest, const StringMap &src,
               bool mangle)
{
    const char *p;

    if (mangle)
        return;

    p = src.Get("server");
    if (p == nullptr)
        return;

    dest.Add("server", p);
}

StringMap
forward_response_headers(struct pool &pool, http_status_t status,
                         const StringMap &src,
                         const char *local_host,
                         const char *session_cookie,
                         const char *(*relocate)(const char *uri, void *ctx),
                         void *relocate_ctx,
                         const struct header_forward_settings &settings)
{
    StringMap dest(pool);

    header_copy_list(src, dest, basic_response_headers);

    forward_link_response_headers(dest, src,
                                  relocate, relocate_ctx,
                                  settings.modes[HEADER_GROUP_LINK]);

    forward_upgrade_response_headers(dest, status, src);

    if (settings.modes[HEADER_GROUP_OTHER] == HEADER_FORWARD_YES)
        forward_other_response_headers(dest, src);

    if (settings.modes[HEADER_GROUP_COOKIE] == HEADER_FORWARD_YES)
        header_copy_list(src, dest, cookie_response_headers);
    else if (settings.modes[HEADER_GROUP_COOKIE] == HEADER_FORWARD_BOTH) {
        if (session_cookie == nullptr)
            header_copy_list(src, dest, cookie_response_headers);
        else
            header_copy_set_cookie_except(dest, src, session_cookie);
    }

    if (settings.modes[HEADER_GROUP_CORS] == HEADER_FORWARD_YES)
        header_copy_list(src, dest, cors_response_headers);

    if (settings.modes[HEADER_GROUP_SECURE] == HEADER_FORWARD_YES)
        forward_secure_headers(dest, src);

    /* RFC 2616 3.8: Product Tokens */
    forward_server(dest, src,
                   settings.modes[HEADER_GROUP_CAPABILITIES] != HEADER_FORWARD_YES);

    if (settings.modes[HEADER_GROUP_IDENTITY] != HEADER_FORWARD_NO)
        forward_via(pool, dest, src, local_host,
                    settings.modes[HEADER_GROUP_IDENTITY] == HEADER_FORWARD_MANGLE);

    if (settings.modes[HEADER_GROUP_TRANSFORMATION] == HEADER_FORWARD_YES)
        forward_transformation_headers(dest, src);

    return dest;
}

void
forward_reveal_user(StringMap &headers,
                    const RealmSession *session)
{
    headers.SecureSet("x-cm4all-beng-user",
                      session != nullptr && session->user != nullptr
                      ? p_strdup(&headers.GetPool(), session->user)
                      : nullptr);
}
