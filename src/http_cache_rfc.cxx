/*
 * Caching HTTP responses.  Implementation for the rules described in
 * RFC 2616.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "http_cache_internal.hxx"
#include "http_util.h"
#include "strref2.h"
#include "strmap.h"
#include "date.h"
#include "tpool.h"
#include "resource-address.h"
#include "lhttp_address.h"

#include <assert.h>
#include <stdlib.h>

static struct strref *
next_item(struct strref *s, struct strref *p)
{
    const char *comma;

    strref_ltrim(s);
    if (strref_is_empty(s))
        return nullptr;

    comma = strref_chr(s, ',');
    if (comma == nullptr) {
        *p = *s;
        strref_clear(s);
    } else {
        strref_set2(p, s->data, comma);
        strref_set2(s, comma + 1, strref_end(s));
    }

    strref_rtrim(p);
    return p;
}

static struct http_cache_info *
http_cache_info_new(struct pool *pool)
{
    auto info = PoolAlloc<http_cache_info>(pool);

    http_cache_info_init(info);
    return info;
}

static bool
resource_address_has_query_string(const struct resource_address *address)
{
    switch (address->type) {
    case RESOURCE_ADDRESS_NONE:
    case RESOURCE_ADDRESS_LOCAL:
    case RESOURCE_ADDRESS_NFS:
        return false;

    case RESOURCE_ADDRESS_HTTP:
        return strchr(address->u.http->path, '?') != nullptr;

    case RESOURCE_ADDRESS_LHTTP:
        return strchr(address->u.lhttp->uri, '?') != nullptr;

    case RESOURCE_ADDRESS_PIPE:
        return false;

    case RESOURCE_ADDRESS_CGI:
    case RESOURCE_ADDRESS_FASTCGI:
    case RESOURCE_ADDRESS_WAS:
        return address->u.cgi->query_string == nullptr ||
            *address->u.cgi->query_string == 0;

    case RESOURCE_ADDRESS_AJP:
        return false;
    }

    /* unreachable */
    assert(false);
    return false;
}

/* check whether the request could produce a cacheable response */
struct http_cache_info *
http_cache_request_evaluate(struct pool *pool,
                            http_method_t method,
                            const struct resource_address *address,
                            const struct strmap *headers,
                            struct istream *body)
{
    struct http_cache_info *info = nullptr;
    const char *p;

    if (method != HTTP_METHOD_GET || body != nullptr)
        /* RFC 2616 13.11 "Write-Through Mandatory" */
        return nullptr;

    if (headers != nullptr) {
        p = strmap_get(headers, "range");
        if (p != nullptr)
            return nullptr;

        /* RFC 2616 14.8: "When a shared cache receives a request
           containing an Authorization field, it MUST NOT return the
           corresponding response as a reply to any other request
           [...] */
        if (strmap_get(headers, "authorization") != nullptr)
            return nullptr;

        p = strmap_get(headers, "cache-control");
        if (p != nullptr) {
            struct strref cc, tmp, *s;

            strref_set_c(&cc, p);

            while ((s = next_item(&cc, &tmp)) != nullptr) {
                if (strref_cmp_literal(s, "no-cache") == 0 ||
                    strref_cmp_literal(s, "no-store") == 0)
                    return nullptr;

                if (strref_cmp_literal(s, "only-if-cached") == 0) {
                    if (info == nullptr)
                        info = http_cache_info_new(pool);
                    info->only_if_cached = true;
                }
            }
        } else {
            p = strmap_get(headers, "pragma");
            if (p != nullptr && strcmp(p, "no-cache") == 0)
                return nullptr;
        }
    }

    if (info == nullptr)
        info = http_cache_info_new(pool);

    info->is_remote = address->type == RESOURCE_ADDRESS_HTTP ||
        address->type == RESOURCE_ADDRESS_AJP;
    info->has_query_string = resource_address_has_query_string(address);

    return info;
}

static bool
vary_fits(struct strmap *vary, const struct strmap *headers)
{
    const struct strmap_pair *pair;

    strmap_rewind(vary);

    while ((pair = strmap_next(vary)) != nullptr) {
        const char *value = strmap_get_checked(headers, pair->key);
        if (value == nullptr)
            value = "";

        if (strcmp(pair->value, value) != 0)
            /* mismatch in one of the "Vary" request headers */
            return false;
    }

    return true;
}

bool
http_cache_document_fits(const struct http_cache_document *document,
                         const struct strmap *headers)
{
    return document->vary == nullptr || vary_fits(document->vary, headers);
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
    time_t t;

    if (p == nullptr)
        return (time_t)-1;

    t = http_date_parse(p);
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
strmap_get_non_empty(const struct strmap *map, const char *key)
{
    const char *value = strmap_get(map, key);
    if (value != nullptr && *value == 0)
        value = nullptr;
    return value;
}

bool
http_cache_response_evaluate(struct http_cache_info *info,
                             http_status_t status, const struct strmap *headers,
                             off_t body_available)
{
    time_t now, offset;
    const char *p;

    if (!http_status_cacheable(status) || headers == nullptr)
        return false;

    if (body_available != (off_t)-1 && body_available > cacheable_size_limit)
        /* too large for the cache */
        return false;

    p = strmap_get(headers, "cache-control");
    if (p != nullptr) {
        struct strref cc, tmp, *s;

        strref_set_c(&cc, p);

        while ((s = next_item(&cc, &tmp)) != nullptr) {
            if (strref_starts_with_n(s, "private", 7) ||
                strref_cmp_literal(s, "no-cache") == 0 ||
                strref_cmp_literal(s, "no-store") == 0)
                return false;

            if (strref_starts_with_n(s, "max-age=", 8)) {
                /* RFC 2616 14.9.3 */
                size_t length = s->length - 8;
                char value[16];
                int seconds;

                if (length >= sizeof(value))
                    continue;

                memcpy(value, s->data + 8, length);
                value[length] = 0;

                seconds = atoi(value);
                if (seconds > 0)
                    info->expires = time(nullptr) + seconds;
            }
        }
    }

    now = time(nullptr);

    if (info->is_remote) {
        p = strmap_get(headers, "date");
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


    if (info->expires == (time_t)-1) {
        /* RFC 2616 14.9.3: "If a response includes both an Expires
           header and a max-age directive, the max-age directive
           overrides the Expires header" */

        info->expires = parse_translate_time(strmap_get(headers, "expires"), offset);
        if (info->expires != (time_t)-1 && info->expires < now)
            cache_log(4, "invalid 'expires' header\n");
    }

    if (info->has_query_string && info->expires == (time_t)-1)
        /* RFC 2616 13.9: "since some applications have traditionally
           used GETs and HEADs with query URLs (those containing a "?"
           in the rel_path part) to perform operations with
           significant side effects, caches MUST NOT treat responses
           to such URIs as fresh unless the server provides an
           explicit expiration time" - this is implemented by not
           storing the resource at all */
        return false;

    info->last_modified = strmap_get(headers, "last-modified");
    info->etag = strmap_get(headers, "etag");

    info->vary = strmap_get_non_empty(headers, "vary");
    if (info->vary != nullptr && strcmp(info->vary, "*") == 0)
        /* RFC 2616 13.6: A Vary header field-value of "*" always
           fails to match and subsequent requests on that resource can
           only be properly interpreted by the origin server. */
        return false;

    return info->expires != (time_t)-1 || info->last_modified != nullptr ||
        info->etag != nullptr;
}

struct strmap *
http_cache_copy_vary(struct pool *pool, const char *vary,
                     const struct strmap *headers)
{
    struct strmap *dest = strmap_new(pool, 16);
    struct pool_mark_state mark;
    char **list;

    if (pool != tpool)
        pool_mark(tpool, &mark);

    for (list = http_list_split(tpool, vary);
         *list != nullptr; ++list) {
        const char *value = strmap_get_checked(headers, *list);
        if (value == nullptr)
            value = "";
        strmap_set(dest, p_strdup(pool, *list),
                   p_strdup(pool, value));
    }

    if (pool != tpool)
        pool_rewind(tpool, &mark);

    return dest;
}

bool
http_cache_prefer_cached(const struct http_cache_document *document,
                         const struct strmap *response_headers)
{
    const char *etag;

    if (document->info.etag == nullptr)
        return false;

    etag = strmap_get_checked(response_headers, "etag");

    /* if the ETags are the same, then the resource hasn't changed,
       but the server was too lazy to check that properly */
    return etag != nullptr && strcmp(etag, document->info.etag) == 0;
}
