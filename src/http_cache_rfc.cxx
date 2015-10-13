/*
 * Caching HTTP responses.  Implementation for the rules described in
 * RFC 2616.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "http_cache_rfc.hxx"
#include "http_cache_document.hxx"
#include "http_cache_internal.hxx"
#include "http_address.hxx"
#include "http_util.hxx"
#include "strmap.hxx"
#include "date.h"
#include "ResourceAddress.hxx"
#include "cgi_address.hxx"
#include "lhttp_address.hxx"
#include "pool.hxx"
#include "util/StringView.hxx"

#include <assert.h>
#include <stdlib.h>

static StringView
next_item(StringView &s)
{
    s.StripLeft();
    if (s.IsEmpty())
        return nullptr;

    StringView result;

    const char *comma = s.Find(',');
    if (comma == nullptr) {
        result = s;
        s.SetEmpty();
    } else {
        result = {s.data, comma};
        s.MoveFront(comma + 1);
    }

    result.StripRight();
    return result;
}

/* check whether the request could produce a cacheable response */
bool
http_cache_request_evaluate(HttpCacheRequestInfo &info,
                            http_method_t method,
                            const ResourceAddress &address,
                            const struct strmap *headers,
                            struct istream *body)
{
    if (method != HTTP_METHOD_GET || body != nullptr)
        /* RFC 2616 13.11 "Write-Through Mandatory" */
        return false;

    if (headers != nullptr) {
        const char *p = headers->Get("range");
        if (p != nullptr)
            return false;

        /* RFC 2616 14.8: "When a shared cache receives a request
           containing an Authorization field, it MUST NOT return the
           corresponding response as a reply to any other request
           [...] */
        if (headers->Get("authorization") != nullptr)
            return false;

        p = headers->Get("cache-control");
        if (p != nullptr) {
            StringView cc = p, s;

            while (!(s = next_item(cc)).IsNull()) {
                if (s.EqualsLiteral("no-cache") || s.EqualsLiteral("no-store"))
                    return false;

                if (s.EqualsLiteral("only-if-cached"))
                    info.only_if_cached = true;
            }
        } else {
            p = headers->Get("pragma");
            if (p != nullptr && strcmp(p, "no-cache") == 0)
                return false;
        }
    }

    info.is_remote = address.type == ResourceAddress::Type::HTTP ||
        address.type == ResourceAddress::Type::AJP;
    info.has_query_string = address.HasQueryString();

    return true;
}

gcc_pure
bool
http_cache_vary_fits(const struct strmap &vary, const struct strmap *headers)
{
    for (const auto &i : vary) {
        const char *value = strmap_get_checked(headers, i.key);
        if (value == nullptr)
            value = "";

        if (strcmp(i.value, value) != 0)
            /* mismatch in one of the "Vary" request headers */
            return false;
    }

    return true;
}

gcc_pure
bool
http_cache_vary_fits(const struct strmap *vary, const struct strmap *headers)
{
    return vary == nullptr || http_cache_vary_fits(*vary, headers);
}

bool
http_cache_request_invalidate(http_method_t method)
{
    /* RFC 2616 13.10 "Invalidation After Updates or Deletions" */
    return method == HTTP_METHOD_PUT || method == HTTP_METHOD_DELETE ||
        method == HTTP_METHOD_POST;
}

static time_t
parse_translate_time(const char *p, time_t offset)
{
    if (p == nullptr)
        return (time_t)-1;

    time_t t = http_date_parse(p);
    if (t != (time_t)-1)
        t += offset;

    return t;
}

/**
 * RFC 2616 13.4
 */
static bool
http_status_cacheable(http_status_t status)
{
    return status == HTTP_STATUS_OK ||
        status == HTTP_STATUS_NON_AUTHORITATIVE_INFORMATION ||
        status == HTTP_STATUS_PARTIAL_CONTENT ||
        status == HTTP_STATUS_MULTIPLE_CHOICES ||
        status == HTTP_STATUS_MOVED_PERMANENTLY ||
        status == HTTP_STATUS_GONE;
}

gcc_pure
static const char *
strmap_get_non_empty(const struct strmap &map, const char *key)
{
    const char *value = map.Get(key);
    if (value != nullptr && *value == 0)
        value = nullptr;
    return value;
}

bool
http_cache_response_evaluate(const HttpCacheRequestInfo &request_info,
                             HttpCacheResponseInfo &info,
                             http_status_t status, const struct strmap *headers,
                             off_t body_available)
{
    const char *p;

    if (!http_status_cacheable(status) || headers == nullptr)
        return false;

    if (body_available != (off_t)-1 && body_available > cacheable_size_limit)
        /* too large for the cache */
        return false;

    info.expires = -1;
    p = headers->Get("cache-control");
    if (p != nullptr) {
        StringView cc = p, s;

        while (!(s = next_item(cc)).IsNull()) {
            if (s.StartsWith("private") ||
                s.EqualsLiteral("no-cache") ||
                s.EqualsLiteral("no-store"))
                return false;

            if (s.StartsWith({"max-age=", 8})) {
                /* RFC 2616 14.9.3 */
                char value[16];
                int seconds;

                StringView param(s.data + 8, s.size - 8);
                if (param.size >= sizeof(value))
                    continue;

                memcpy(value, param.data, param.size);
                value[param.size] = 0;

                seconds = atoi(value);
                if (seconds > 0)
                    info.expires = time(nullptr) + seconds;
            }
        }
    }

    const time_t now = time(nullptr);

    time_t offset;
    if (request_info.is_remote) {
        p = headers->Get("date");
        if (p == nullptr)
            /* we cannot determine whether to cache a resource if the
               server does not provide its system time */
            return false;

        time_t date = http_date_parse(p);
        if (date == (time_t)-1)
            return false;

        offset = now - date;
    } else
        offset = 0;


    if (info.expires == (time_t)-1) {
        /* RFC 2616 14.9.3: "If a response includes both an Expires
           header and a max-age directive, the max-age directive
           overrides the Expires header" */

        info.expires = parse_translate_time(headers->Get("expires"), offset);
        if (info.expires != (time_t)-1 && info.expires < now)
            cache_log(4, "invalid 'expires' header\n");
    }

    if (request_info.has_query_string && info.expires == (time_t)-1)
        /* RFC 2616 13.9: "since some applications have traditionally
           used GETs and HEADs with query URLs (those containing a "?"
           in the rel_path part) to perform operations with
           significant side effects, caches MUST NOT treat responses
           to such URIs as fresh unless the server provides an
           explicit expiration time" - this is implemented by not
           storing the resource at all */
        return false;

    info.last_modified = headers->Get("last-modified");
    info.etag = headers->Get("etag");

    info.vary = strmap_get_non_empty(*headers, "vary");
    if (info.vary != nullptr && strcmp(info.vary, "*") == 0)
        /* RFC 2616 13.6: A Vary header field-value of "*" always
           fails to match and subsequent requests on that resource can
           only be properly interpreted by the origin server. */
        return false;

    return info.expires != (time_t)-1 || info.last_modified != nullptr ||
        info.etag != nullptr;
}

void
http_cache_copy_vary(struct strmap &dest, struct pool &pool, const char *vary,
                     const struct strmap *request_headers)
{
    for (char **list = http_list_split(&pool, vary);
         *list != nullptr; ++list) {
        const char *name = *list;
        const char *value = strmap_get_checked(request_headers, name);
        if (value == nullptr)
            value = "";
        else
            value = p_strdup(&pool, value);
        dest.Set(name, value);
    }
}

bool
http_cache_prefer_cached(const HttpCacheDocument &document,
                         const struct strmap *response_headers)
{
    if (document.info.etag == nullptr)
        return false;

    const char *etag = strmap_get_checked(response_headers, "etag");

    /* if the ETags are the same, then the resource hasn't changed,
       but the server was too lazy to check that properly */
    return etag != nullptr && strcmp(etag, document.info.etag) == 0;
}
