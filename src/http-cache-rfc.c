/*
 * Caching HTTP responses.  Implementation for the rules described in
 * RFC 2616.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "http-cache-internal.h"
#include "http-util.h"
#include "strref2.h"
#include "strmap.h"
#include "date.h"
#include "tpool.h"

#include <stdlib.h>

static struct strref *
next_item(struct strref *s, struct strref *p)
{
    const char *comma;

    strref_ltrim(s);
    if (strref_is_empty(s))
        return NULL;

    comma = strref_chr(s, ',');
    if (comma == NULL) {
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
http_cache_info_new(pool_t pool)
{
    struct http_cache_info *info = p_malloc(pool, sizeof(*info));

    http_cache_info_init(info);
    return info;
}

/* check whether the request could produce a cacheable response */
struct http_cache_info *
http_cache_request_evaluate(pool_t pool,
                            http_method_t method, const char *uri,
                            const struct strmap *headers,
                            istream_t body)
{
    struct http_cache_info *info = NULL;
    const char *p;

    if (method != HTTP_METHOD_GET || body != NULL)
        /* RFC 2616 13.11 "Write-Through Mandatory" */
        return NULL;

    if (headers != NULL) {
        p = strmap_get(headers, "range");
        if (p != NULL)
            return NULL;

        p = strmap_get(headers, "cache-control");
        if (p != NULL) {
            struct strref cc, tmp, *s;

            strref_set_c(&cc, p);

            while ((s = next_item(&cc, &tmp)) != NULL) {
                if (strref_cmp_literal(s, "no-cache") == 0 ||
                    strref_cmp_literal(s, "no-store") == 0)
                    return NULL;

                if (strref_cmp_literal(s, "only-if-cached") == 0) {
                    if (info == NULL)
                        info = http_cache_info_new(pool);
                    info->only_if_cached = true;
                }
            }
        } else {
            p = strmap_get(headers, "pragma");
            if (p != NULL && strcmp(p, "no-cache") == 0)
                return NULL;
        }
    }

    if (info == NULL)
        info = http_cache_info_new(pool);

    info->has_query_string = strchr(uri, '?') != NULL;

    return info;
}

static bool
vary_fits(struct strmap *vary, const struct strmap *headers)
{
    const struct strmap_pair *pair;

    strmap_rewind(vary);

    while ((pair = strmap_next(vary)) != NULL) {
        const char *value = strmap_get_checked(headers, pair->key);
        if (value == NULL)
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
    return document->vary == NULL || vary_fits(document->vary, headers);
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

    if (p == NULL)
        return (time_t)-1;

    t = http_date_parse(p);
    if (t != (time_t)-1)
        t += offset;

    return t;
}

bool
http_cache_response_evaluate(struct http_cache_info *info,
                             http_status_t status, const struct strmap *headers,
                             off_t body_available)
{
    time_t date, now, offset;
    const char *p;

    if (status != HTTP_STATUS_OK || body_available == 0)
        return false;

    if (body_available != (off_t)-1 && body_available > cacheable_size_limit)
        /* too large for the cache */
        return false;

    p = strmap_get(headers, "cache-control");
    if (p != NULL) {
        struct strref cc, tmp, *s;

        strref_set_c(&cc, p);

        while ((s = next_item(&cc, &tmp)) != NULL) {
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
                    info->expires = time(NULL) + seconds;
            }
        }
    }

    p = strmap_get(headers, "date");
    if (p == NULL)
        /* we cannot determine wether to cache a resource if the
           server does not provide its system time */
        return false;
    date = http_date_parse(p);
    if (date == (time_t)-1)
        return false;

    now = time(NULL);
    offset = now - date;

    if (info->expires == (time_t)-1) {
        /* RFC 2616 14.9.3: "If a response includes both an Expires
           header and a max-age directive, the max-age directive
           overrides the Expires header" */

        info->expires = parse_translate_time(strmap_get(headers, "expires"), offset);
        if (info->expires != (time_t)-1 && info->expires < now)
            cache_log(2, "invalid 'expires' header\n");
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

    info->vary = strmap_get(headers, "vary");
    if (info->vary != NULL && strcmp(info->vary, "*") == 0)
        /* RFC 2616 13.6: A Vary header field-value of "*" always
           fails to match and subsequent requests on that resource can
           only be properly interpreted by the origin server. */
        return false;

    return info->expires != (time_t)-1 || info->last_modified != NULL ||
        info->etag != NULL;
}

struct strmap *
http_cache_copy_vary(pool_t pool, const char *vary,
                     const struct strmap *headers)
{
    struct strmap *dest = strmap_new(pool, 16);
    struct pool_mark mark;
    char **list;

    if (pool != tpool)
        pool_mark(tpool, &mark);

    for (list = http_list_split(tpool, vary);
         *list != NULL; ++list) {
        const char *value = strmap_get_checked(headers, *list);
        if (value == NULL)
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

    if (document->info.etag == NULL)
        return false;

    etag = strmap_get_checked(response_headers, "etag");

    /* if the ETags are the same, then the resource hasn't changed,
       but the server was too lazy to check that properly */
    return etag != NULL && strcmp(etag, document->info.etag) == 0;
}
