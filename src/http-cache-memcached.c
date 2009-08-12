/*
 * Caching HTTP responses.  Memcached backend.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "http-cache-internal.h"
#include "memcached-stock.h"
#include "growing-buffer.h"

#include <glib.h>

#include <string.h>

struct http_cache_memcached_request {
    pool_t pool;

    const char *uri;

    struct strmap *request_headers;

    union {
        struct memcached_set_extras set;
    } extras;

    union {
        http_cache_memcached_get_t get;
        http_cache_memcached_put_t put;
    } callback;

    void *callback_ctx;
};

static void
http_cache_memcached_get_callback(enum memcached_response_status status,
                                  istream_t value, void *ctx)
{
    struct http_cache_memcached_request *request = ctx;

    if (status != MEMCACHED_STATUS_NO_ERROR || value == NULL) {
        if (value != NULL)
            istream_close(value);

        request->callback.get(NULL, NULL, request->callback_ctx);
        return;
    }

    request->callback.get(NULL, value, request->callback_ctx);
}

void
http_cache_memcached_get(pool_t pool, struct memcached_stock *stock,
                         const char *uri, struct strmap *request_headers,
                         http_cache_memcached_get_t callback,
                         void *callback_ctx,
                         struct async_operation_ref *async_ref)
{
    struct http_cache_memcached_request *request = p_malloc(pool, sizeof(*request));

    request->pool = pool;
    request->uri = uri;
    request->request_headers = request_headers;
    request->callback.get = callback;
    request->callback_ctx = callback_ctx;

    memcached_stock_invoke(pool, stock, MEMCACHED_OPCODE_GET,
                           NULL, 0,
                           uri, strlen(uri),
                           NULL, 0,
                           http_cache_memcached_get_callback, request,
                           async_ref);
}

static void
http_cache_memcached_put_callback(G_GNUC_UNUSED enum memcached_response_status status,
                                  istream_t value, void *ctx)
{
    struct http_cache_memcached_request *request = ctx;

    if (value != NULL)
        istream_close(value);

    request->callback.put(request->callback_ctx);
}

void
http_cache_memcached_put(pool_t pool, struct memcached_stock *stock,
                         const char *uri, const struct growing_buffer *body,
                         http_cache_memcached_put_t callback, void *callback_ctx,
                         struct async_operation_ref *async_ref)
{
    struct http_cache_memcached_request *request = p_malloc(pool, sizeof(*request));
    size_t value_size;
    void *value = growing_buffer_dup(body, pool, &value_size);

    request->extras.set.flags = 0;
    request->extras.set.expiration = g_htonl(300); /* XXX */

    request->callback.put = callback;
    request->callback_ctx = callback_ctx;

    memcached_stock_invoke(pool, stock,
                           MEMCACHED_OPCODE_SET,
                           &request->extras, sizeof(request->extras),
                           uri, strlen(uri),
                           value, value_size,
                           http_cache_memcached_put_callback, request,
                           async_ref);
}
