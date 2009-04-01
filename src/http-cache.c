/*
 * Caching HTTP responses.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "http-cache.h"
#include "cache.h"
#include "http-request.h"
#include "header-writer.h"
#include "strmap.h"
#include "http-response.h"
#include "date.h"
#include "uri-address.h"
#include "strref2.h"
#include "growing-buffer.h"
#include "tpool.h"
#include "http-util.h"
#include "async.h"

#include <string.h>
#include <time.h>
#include <stdlib.h>

#ifdef CACHE_LOG
#include <daemon/log.h>
#define cache_log(...) daemon_log(__VA_ARGS__)
#else
#define cache_log(...) do {} while (0)
#endif

static const off_t cacheable_size_limit = 256 * 1024;

struct http_cache {
    pool_t pool;
    struct cache *cache;
    struct hstock *stock;
};

struct http_cache_info {
    bool only_if_cached;

    /** when will the cached resource expire? (beng-proxy time) */
    time_t expires;

    /** when was the cached resource last modified on the widget
        server? (widget server time) */
    const char *last_modified;

    const char *etag;

    const char *vary;
};

struct http_cache_item {
    struct cache_item item;

    pool_t pool;

    struct http_cache_info info;

    struct strmap *vary;

    http_status_t status;
    struct strmap *headers;
    unsigned char *data;
};

struct http_cache_request {
    pool_t pool, caller_pool;
    struct http_cache *cache;
    const char *url;
    struct strmap *headers;
    struct http_response_handler_ref handler;

    struct http_cache_item *item;
    struct http_cache_info *info;

    struct {
        http_status_t status;
        struct strmap *headers;
        istream_t input;
        size_t length;
        struct growing_buffer *output;
    } response;

    struct async_operation operation;
    struct async_operation_ref async_ref;
};


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

    info->only_if_cached = false;
    info->expires = (time_t)-1;
    info->last_modified = NULL;
    info->etag = NULL;
    return info;
}

/* check whether the request could produce a cacheable response */
static struct http_cache_info *
http_cache_request_evaluate(pool_t pool,
                            http_method_t method,
                            struct strmap *headers,
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
    return info;
}

static bool
vary_fits(struct strmap *vary, const struct strmap *headers)
{
    const struct strmap_pair *pair;

    strmap_rewind(vary);

    while ((pair = strmap_next(vary)) != NULL) {
        const char *value = headers == NULL ? NULL : strmap_get(headers, pair->key);
        if (value == NULL)
            value = "";

        if (strcmp(pair->value, value) != 0)
            /* mismatch in one of the "Vary" request headers */
            return false;
    }

    return true;
}

/**
 * Checks whether the specified cache item fits the current request.
 * This is not true if the Vary headers mismatch.
 */
static bool
http_cache_item_fits(const struct http_cache_item *item,
                     struct strmap *headers)
{
    return item->vary == NULL || vary_fits(item->vary, headers);
}

/* check whether the request should invalidate the existing cache */
static bool
http_cache_request_invalidate(http_method_t method)
{
    /* RFC 2616 13.10 "Invalidation After Updates or Deletions" */
    return method == HTTP_METHOD_PUT || method == HTTP_METHOD_DELETE ||
        method == HTTP_METHOD_POST;
}

static void
http_cache_copy_info(pool_t pool, struct http_cache_info *dest,
                     const struct http_cache_info *src)
{
    dest->expires = src->expires;

    if (src->last_modified != NULL)
        dest->last_modified = p_strdup(pool, src->last_modified);
    else
        dest->last_modified = NULL;

    if (src->etag != NULL)
        dest->etag = p_strdup(pool, src->etag);
    else
        dest->etag = NULL;

    if (src->vary != NULL)
        dest->vary = p_strdup(pool, src->vary);
    else
        dest->vary = NULL;
}

static struct http_cache_info *
http_cache_info_dup(pool_t pool, const struct http_cache_info *src)
{
    struct http_cache_info *dest = p_malloc(pool, sizeof(*dest));

    http_cache_copy_info(pool, dest, src);
    return dest;
}

static struct http_cache_request *
http_cache_request_dup(pool_t pool, const struct http_cache_request *src)
{
    struct http_cache_request *dest = p_malloc(pool, sizeof(*dest));

    dest->pool = pool;
    dest->caller_pool = src->caller_pool;
    dest->cache = src->cache;
    dest->url = p_strdup(pool, src->url);
    dest->headers = src->headers == NULL
        ? NULL : strmap_dup(pool, src->headers);
    dest->handler = src->handler;
    dest->info = http_cache_info_dup(pool, src->info);
    return dest;
}

/**
 * Copy all request headers mentioned in the Vary response header to a
 * new strmap.
 */
static struct strmap *
http_cache_copy_vary(pool_t pool, const char *vary, struct strmap *headers)
{
    struct strmap *dest = strmap_new(pool, 16);
    struct pool_mark mark;
    char **list;

    pool_mark(tpool, &mark);

    for (list = http_list_split(tpool, vary);
         *list != NULL; ++list) {
        const char *value = headers != NULL
            ? strmap_get(headers, *list)
            : NULL;
        if (value == NULL)
            value = "";
        strmap_set(dest, p_strdup(pool, *list),
                   p_strdup(pool, value));
    }

    pool_rewind(tpool, &mark);

    return dest;
}

static bool
http_cache_item_match(const struct cache_item *_item, void *ctx)
{
    const struct http_cache_item *item =
        (const struct http_cache_item *)_item;
    struct strmap *headers = ctx;

    return http_cache_item_fits(item, headers);
}

static void
http_cache_put(struct http_cache_request *request)
{
    pool_t pool;
    struct http_cache_item *item;
    time_t expires;

    assert(request != NULL);
    assert(request->info != NULL);

    cache_log(4, "http_cache: put %s\n", request->url);

    pool = pool_new_linear(request->cache->pool, "http_cache_item", 1024);
    item = p_malloc(pool, sizeof(*item));

    if (request->info->expires == (time_t)-1)
        /* there is no Expires response header; keep it in the cache
           for 1 hour, but check with If-Modified-Since */
        expires = time(NULL) + 3600;
    else
        expires = request->info->expires;

    cache_item_init(&item->item, expires, request->response.length);

    item->pool = pool;
    http_cache_copy_info(pool, &item->info, request->info);

    item->vary = item->info.vary != NULL
        ? http_cache_copy_vary(pool, item->info.vary, request->headers)
        : NULL;

    item->status = request->response.status;
    item->headers = strmap_dup(pool, request->response.headers);

    if (item->item.size == 0) {
        item->data = NULL;
    } else {
        unsigned char *dest;
        const void *src;
        size_t length;

        item->data = dest = p_malloc(pool, item->item.size);
        while ((src = growing_buffer_read(request->response.output, &length)) != NULL) {
            memcpy(dest, src, length);
            dest += length;
            growing_buffer_consume(request->response.output, length);
        }
    }

    cache_put_match(request->cache->cache, p_strdup(pool, request->url),
                    &item->item,
                    http_cache_item_match, request->headers);
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

/** check whether the HTTP response should be put into the cache */
static bool
http_cache_response_evaluate(struct http_cache_info *info,
                             http_status_t status, struct strmap *headers,
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

    info->last_modified = strmap_get(headers, "last-modified");
    info->etag = strmap_get(headers, "etag");

    info->vary = strmap_get(headers, "vary");
    if (info->vary != NULL && strcmp(info->vary, "*") == 0)
        /* RFC 2616 13.6: A Vary header field-value of "*" always
           fails to match and subsequent requests on that resource can
           only be properly interpreted by the origin server. */
        return false;

    return info->expires != (time_t)-1 || info->last_modified != NULL;
}

static void
http_cache_serve(struct http_cache_item *item,
                 pool_t pool,
                 const char *url, istream_t body,
                 const struct http_response_handler *handler,
                 void *handler_ctx);


/*
 * istream handler
 *
 */

static size_t
http_cache_response_body_data(const void *data, size_t length, void *ctx)
{
    struct http_cache_request *request = ctx;

    request->response.length += length;
    if (request->response.length > (size_t)cacheable_size_limit) {
        istream_close(request->response.input);
        return 0;
    }

    growing_buffer_write_buffer(request->response.output, data, length);
    return length;
}

static void
http_cache_response_body_eof(void *ctx)
{
    struct http_cache_request *request = ctx;

    /* the request was successful, and all of the body data has been
       saved: add it to the cache */
    http_cache_put(request);

    pool_unref(request->pool);
}

static void
http_cache_response_body_abort(void *ctx)
{
    struct http_cache_request *request = ctx;

    cache_log(4, "http_cache: body_abort %s\n", request->url);

    pool_unref(request->pool);
}

static const struct istream_handler http_cache_response_body_handler = {
    .data = http_cache_response_body_data,
    .eof = http_cache_response_body_eof,
    .abort = http_cache_response_body_abort,
};


/*
 * http response handler
 *
 */

static void
http_cache_response_response(http_status_t status, struct strmap *headers,
                             istream_t body,
                             void *ctx)
{
    struct http_cache_request *request = ctx;
    off_t available;
    pool_t caller_pool;

    if (request->item != NULL && status == HTTP_STATUS_NOT_MODIFIED) {
        assert(body == NULL);

        cache_log(5, "http_cache: not_modified %s\n", request->url);
        http_cache_serve(request->item, request->pool,
                         request->url, NULL,
                         request->handler.handler, request->handler.ctx);
        pool_unref(request->caller_pool);
        return;
    }

    if (request->item != NULL) {
        cache_remove_item(request->cache->cache, request->url,
                          &request->item->item);
        cache_item_unlock(request->cache->cache, &request->item->item);
    }

    available = body == NULL ? 0 : istream_available(body, true);

    if (!http_cache_response_evaluate(request->info,
                                      status, headers, available)) {
        /* don't cache response */
        cache_log(4, "http_cache: nocache %s\n", request->url);

        http_response_handler_invoke_response(&request->handler, status,
                                              headers, body);
        pool_unref(request->caller_pool);
        return;
    }

    if (body == NULL) {
        request->response.output = NULL;
        http_cache_put(request);
    } else {
        pool_t pool;
        size_t buffer_size;

        /* move all this stuff to a new pool, so istream_tee's second
           head can continue to fill the cache even if our caller gave
           up on it */
        pool = pool_new_linear(request->cache->pool, "http_cache_tee", 1024);
        request = http_cache_request_dup(pool, request);

        /* tee the body: one goes to our client, and one goes into the
           cache */
        body = istream_tee_new(request->pool, body, false);

        request->response.status = status;
        request->response.headers = strmap_dup(request->pool, headers);
        request->response.length = 0;

        istream_assign_handler(&request->response.input,
                               istream_tee_second(body),
                               &http_cache_response_body_handler, request,
                               0);

        if (available == (off_t)-1 || available < 256)
            buffer_size = 1024;
        else if (available > 16384)
            buffer_size = 16384;
        else
            buffer_size = (size_t)available;
        request->response.output = growing_buffer_new(request->pool,
                                                      buffer_size);
    }

    caller_pool = request->caller_pool;
    http_response_handler_invoke_response(&request->handler, status,
                                          headers, body);
    pool_unref(caller_pool);
}

static void 
http_cache_response_abort(void *ctx)
{
    struct http_cache_request *request = ctx;

    cache_log(4, "http_cache: response_abort %s\n", request->url);

    if (request->item != NULL)
        cache_item_unlock(request->cache->cache, &request->item->item);

    http_response_handler_invoke_abort(&request->handler);
    pool_unref(request->caller_pool);
}

static const struct http_response_handler http_cache_response_handler = {
    .response = http_cache_response_response,
    .abort = http_cache_response_abort,
};


/*
 * async operation
 *
 */

static struct http_cache_request *
async_to_request(struct async_operation *ao)
{
    return (struct http_cache_request*)(((char*)ao) - offsetof(struct http_cache_request, operation));
}

static void
http_cache_abort(struct async_operation *ao)
{
    struct http_cache_request *request = async_to_request(ao);
    pool_t caller_pool = request->caller_pool;

    if (request->item != NULL)
        cache_item_unlock(request->cache->cache, &request->item->item);

    async_abort(&request->async_ref);

    /* the async_abort() call may have destroyed request->pool, so
       we're using a local variable instead of dereferencing
       request->caller_pool */
    pool_unref(caller_pool);
}

static const struct async_operation_class http_cache_async_operation = {
    .abort = http_cache_abort,
};


/*
 * cache_class
 *
 */

static bool
http_cache_item_validate(struct cache_item *_item)
{
    struct http_cache_item *item = (struct http_cache_item *)_item;

    (void)item;
    return true;
}

static void
http_cache_item_destroy(struct cache_item *_item)
{
    struct http_cache_item *item = (struct http_cache_item *)_item;

    pool_unref(item->pool);
}

static const struct cache_class http_cache_class = {
    .validate = http_cache_item_validate,
    .destroy = http_cache_item_destroy,
};


/*
 * constructor and public methods
 *
 */

struct http_cache *
http_cache_new(pool_t pool, size_t max_size,
               struct hstock *tcp_stock)
{
    struct http_cache *cache = p_malloc(pool, sizeof(*cache));
    cache->pool = pool;
    cache->cache = cache_new(pool, &http_cache_class, max_size);
    cache->stock = tcp_stock;
    return cache;
}

void
http_cache_close(struct http_cache *cache)
{
    cache_close(cache->cache);
}

static void
http_cache_miss(struct http_cache *cache, pool_t caller_pool,
                struct http_cache_info *info,
                http_method_t method,
                struct uri_with_address *uwa,
                struct strmap *headers, istream_t body,
                const struct http_response_handler *handler,
                void *handler_ctx,
                struct async_operation_ref *async_ref)
{
    pool_t pool;
    struct http_cache_request *request;

    if (info->only_if_cached) {
        http_response_handler_direct_response(handler, handler_ctx,
                                              HTTP_STATUS_GATEWAY_TIMEOUT,
                                              NULL, NULL);
        return;
    }

    /* the cache request may live longer than the caller pool, so
       allocate a new pool for it from cache->pool */
    pool = pool_new_linear(cache->pool, "http_cache_request", 8192);

    request = p_malloc(pool, sizeof(*request));
    request->pool = pool;
    request->caller_pool = caller_pool;
    request->cache = cache;
    request->url = uwa->uri;
    request->headers = headers == NULL ? NULL : strmap_dup(pool, headers);
    http_response_handler_set(&request->handler, handler, handler_ctx);

    request->item = NULL;
    request->info = info;

    cache_log(4, "http_cache: miss %s\n", uwa->uri);

    async_init(&request->operation, &http_cache_async_operation);
    async_ref_set(async_ref, &request->operation);

    pool_ref(caller_pool);
    http_request(pool, cache->stock,
                 method, uwa,
                 headers == NULL ? NULL : headers_dup(pool, headers), body,
                 &http_cache_response_handler, request,
                 &request->async_ref);
    pool_unref(pool);
}

static void
http_cache_serve(struct http_cache_item *item,
                 pool_t pool,
                 const char *url __attr_unused, istream_t body,
                 const struct http_response_handler *handler,
                 void *handler_ctx)
{
    struct http_response_handler_ref handler_ref;
    istream_t response_body;

    if (body != NULL)
        istream_close(body);

    cache_log(4, "http_cache: serve %s\n", url);

    http_response_handler_set(&handler_ref, handler, handler_ctx);

    /* XXX hold reference on item */
    response_body = istream_memory_new(pool, item->data, item->item.size);
    http_response_handler_invoke_response(&handler_ref, item->status,
                                          item->headers, response_body);
}

static void
http_cache_test(struct http_cache *cache, pool_t caller_pool,
                struct http_cache_info *info,
                struct http_cache_item *item,
                http_method_t method,
                struct uri_with_address *uwa,
                struct strmap *headers, istream_t body,
                const struct http_response_handler *handler,
                void *handler_ctx,
                struct async_operation_ref *async_ref)
{
    /* the cache request may live longer than the caller pool, so
       allocate a new pool for it from cache->pool */
    pool_t pool = pool_new_linear(cache->pool, "http_cache_request", 8192);
    struct http_cache_request *request = p_malloc(pool,
                                                  sizeof(*request));
    request->pool = pool;
    request->caller_pool = caller_pool;
    request->cache = cache;
    request->url = uwa->uri;
    request->headers = headers == NULL ? NULL : strmap_dup(pool, headers);
    http_response_handler_set(&request->handler, handler, handler_ctx);

    cache_item_lock(&item->item);
    request->item = item;
    request->info = info;

    cache_log(4, "http_cache: test %s\n", uwa->uri);

    if (headers == NULL)
        headers = strmap_new(pool, 16);

    if (item->info.last_modified != NULL)
        strmap_set(headers, "if-modified-since", item->info.last_modified);

    if (item->info.etag != NULL)
        strmap_set(headers, "if-none-match", item->info.etag);

    async_init(&request->operation, &http_cache_async_operation);
    async_ref_set(async_ref, &request->operation);

    pool_ref(caller_pool);
    http_request(pool, cache->stock,
                 method, uwa,
                 headers_dup(pool, headers), body,
                 &http_cache_response_handler, request,
                 &request->async_ref);
    pool_unref(pool);
}

static void
http_cache_found(struct http_cache *cache,
                 struct http_cache_info *info,
                 struct http_cache_item *item,
                 pool_t pool,
                 http_method_t method,
                 struct uri_with_address *uwa,
                 struct strmap *headers, istream_t body,
                 const struct http_response_handler *handler,
                 void *handler_ctx,
                 struct async_operation_ref *async_ref)
{
    if (info->only_if_cached ||
        (item->info.expires != (time_t)-1 && item->info.expires >= time(NULL)))
        http_cache_serve(item, pool, uwa->uri, body, handler, handler_ctx);
    else
        http_cache_test(cache, pool, info, item,
                        method, uwa, headers, body,
                        handler, handler_ctx, async_ref);
}

void
http_cache_request(struct http_cache *cache,
                   pool_t pool,
                   http_method_t method,
                   struct uri_with_address *uwa,
                   struct strmap *headers, istream_t body,
                   const struct http_response_handler *handler,
                   void *handler_ctx,
                   struct async_operation_ref *async_ref)
{
    struct http_cache_info *info;

    info = http_cache_request_evaluate(pool, method, headers, body);
    if (info != NULL) {
        struct http_cache_item *item
            = (struct http_cache_item *)cache_get_match(cache->cache, uwa->uri,
                                                        http_cache_item_match,
                                                        headers);

        if (item == NULL)
            http_cache_miss(cache, pool, info,
                            method, uwa, headers, body,
                            handler, handler_ctx, async_ref);
        else
            http_cache_found(cache, info, item, pool,
                             method, uwa, headers, body,
                             handler, handler_ctx, async_ref);
    } else {
        struct growing_buffer *headers2;

        if (http_cache_request_invalidate(method))
            cache_remove(cache->cache, uwa->uri);

        cache_log(4, "http_cache: ignore %s\n", uwa->uri);

        headers2 = headers == NULL
            ? NULL : headers_dup(pool, headers);

        http_request(pool, cache->stock,
                     method, uwa,
                     headers2, body,
                     handler, handler_ctx,
                     async_ref);
    }
}
