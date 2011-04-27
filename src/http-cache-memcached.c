/*
 * Caching HTTP responses.  Memcached backend.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "http-cache-internal.h"
#include "http-cache-choice.h"
#include "memcached-stock.h"
#include "memcached-client.h"
#include "growing-buffer.h"
#include "serialize.h"
#include "sink-header.h"
#include "strref.h"
#include "strmap.h"
#include "tpool.h"
#include "background.h"
#include "istream-gb.h"

#include <glib.h>

#include <string.h>

enum http_cache_memcached_type {
    TYPE_DOCUMENT = 2,
};

struct http_cache_memcached_request {
    pool_t pool;

    struct memcached_stock *stock;

    pool_t background_pool;
    struct background_manager *background;

    const char *uri;

    struct strmap *request_headers;

    bool in_choice;
    struct http_cache_choice *choice;

    uint32_t header_size;

    union {
        struct memcached_set_extras set;
    } extras;

    union {
        http_cache_memcached_flush_t flush;
        http_cache_memcached_get_t get;
        http_cache_memcached_put_t put;
    } callback;

    void *callback_ctx;

    struct async_operation_ref *async_ref;
};

/*
 * Public functions and memcached-client callbacks
 *
 */

static void
http_cache_memcached_flush_response(enum memcached_response_status status,
                                    G_GNUC_UNUSED const void *extras,
                                    G_GNUC_UNUSED size_t extras_length,
                                    G_GNUC_UNUSED const void *key,
                                    G_GNUC_UNUSED size_t key_length,
                                    istream_t value, void *ctx)
{
    struct http_cache_memcached_request *request = ctx;

    if (value != NULL)
        istream_close_unused(value);

    request->callback.flush(status == MEMCACHED_STATUS_NO_ERROR,
                            NULL, request->callback_ctx);
}

static void
http_cache_memcached_flush_error(GError *error, void *ctx)
{
    struct http_cache_memcached_request *request = ctx;

    request->callback.flush(false, error, request->callback_ctx);
}

static const struct memcached_client_handler http_cache_memcached_flush_handler = {
    .response = http_cache_memcached_flush_response,
    .error = http_cache_memcached_flush_error,
};

void
http_cache_memcached_flush(pool_t pool, struct memcached_stock *stock,
                           http_cache_memcached_flush_t callback,
                           void *callback_ctx,
                           struct async_operation_ref *async_ref)
{
    struct http_cache_memcached_request *request = p_malloc(pool, sizeof(*request));

    request->pool = pool;
    request->callback.flush = callback;
    request->callback_ctx = callback_ctx;

    memcached_stock_invoke(pool, stock, MEMCACHED_OPCODE_FLUSH,
                           NULL, 0,
                           NULL, 0,
                           NULL,
                           &http_cache_memcached_flush_handler, request,
                           async_ref);
}

static struct http_cache_document *
mcd_deserialize_document(pool_t pool, struct strref *header,
                         const struct strmap *request_headers)
{
    struct http_cache_document *document;

    document = p_malloc(pool, sizeof(*document));

    http_cache_info_init(&document->info);

    document->info.expires = deserialize_uint64(header);
    document->vary = deserialize_strmap(header, pool);
    document->status = deserialize_uint16(header);
    document->headers = deserialize_strmap(header, pool);

    if (strref_is_null(header) || !http_status_is_valid(document->status))
        return NULL;

    document->info.last_modified =
        strmap_get_checked(document->headers, "last-modified");
    document->info.etag = strmap_get_checked(document->headers, "etag");
    document->info.vary = strmap_get_checked(document->headers, "vary");

    if (!http_cache_document_fits(document, request_headers))
        /* Vary mismatch */
        return NULL;

    return document;
}

static void
http_cache_memcached_get_response(enum memcached_response_status status,
                                  const void *extras, size_t extras_length,
                                  const void *key, size_t key_length,
                                  istream_t value, void *ctx);

static void
http_cache_memcached_get_error(GError *error, void *ctx)
{
    struct http_cache_memcached_request *request = ctx;

    request->callback.get(NULL, 0, error, request->callback_ctx);
}

static const struct memcached_client_handler http_cache_memcached_get_handler = {
    .response = http_cache_memcached_get_response,
    .error = http_cache_memcached_get_error,
};

static void
background_callback(GError *error, void *ctx)
{
    struct background_job *job = ctx;

    if (error != NULL) {
        cache_log(2, "http-cache: memcached failed: %s\n", error->message);
        g_error_free(error);
    }

    background_manager_remove(job);
}

static void
mcd_choice_get_callback(const char *key, bool unclean,
                        GError *error, void *ctx)
{
    struct http_cache_memcached_request *request = ctx;

    if (unclean) {
        /* this choice record is unclean - start cleanup as a
           background job */
        pool_t pool = pool_new_linear(request->background_pool,
                                      "http_cache_choice_cleanup", 8192);
        struct background_job *job = p_malloc(pool, sizeof(*job));

        http_cache_choice_cleanup(pool, request->stock, request->uri,
                                  background_callback, job,
                                  background_job_add(request->background,
                                                     job));
        pool_unref(pool);
    }

    if (key == NULL) {
        if (error != NULL) {
            cache_log(2, "http-cache: GET from memcached failed: %s\n", error->message);
            g_error_free(error);
        }

        request->callback.get(NULL, 0, NULL, request->callback_ctx);
        return;
    }

    request->in_choice = true;
    memcached_stock_invoke(request->pool, request->stock, MEMCACHED_OPCODE_GET,
                           NULL, 0,
                           key, strlen(key),
                           NULL,
                           &http_cache_memcached_get_handler, request,
                           request->async_ref);
}

static void
http_cache_memcached_header_done(void *header_ptr, size_t length,
                                 istream_t tail, void *ctx)
{
    struct http_cache_memcached_request *request = ctx;
    struct strref header;
    enum http_cache_memcached_type type;
    struct http_cache_document *document;

    strref_set(&header, header_ptr, length);

    type = deserialize_uint32(&header);
    switch (type) {
    case TYPE_DOCUMENT:
        document = mcd_deserialize_document(request->pool, &header,
                                            request->request_headers);
        if (document == NULL) {
            if (request->in_choice)
                break;

            istream_close_unused(tail);

            http_cache_choice_get(request->pool, request->stock,
                                  request->uri, request->request_headers,
                                  mcd_choice_get_callback, request,
                                  request->async_ref);
            return;
        }

        request->callback.get(document, tail, NULL, request->callback_ctx);
        return;
    }

    istream_close_unused(tail);
    request->callback.get(NULL, 0, NULL, request->callback_ctx);
}

static void
http_cache_memcached_header_error(GError *error, void *ctx)
{
    struct http_cache_memcached_request *request = ctx;

    request->callback.get(NULL, 0, error, request->callback_ctx);
}

static const struct sink_header_handler http_cache_memcached_header_handler = {
    .done = http_cache_memcached_header_done,
    .error = http_cache_memcached_header_error,
};

static void
http_cache_memcached_get_response(enum memcached_response_status status,
                                  G_GNUC_UNUSED const void *extras,
                                  G_GNUC_UNUSED size_t extras_length,
                                  G_GNUC_UNUSED const void *key,
                                  G_GNUC_UNUSED size_t key_length,
                                  istream_t value, void *ctx)
{
    struct http_cache_memcached_request *request = ctx;

    if (status == MEMCACHED_STATUS_KEY_NOT_FOUND && !request->in_choice) {
        if (value != NULL)
            istream_close_unused(value);

        http_cache_choice_get(request->pool, request->stock,
                              request->uri, request->request_headers,
                              mcd_choice_get_callback, request,
                              request->async_ref);
        return;
    }

    if (status != MEMCACHED_STATUS_NO_ERROR || value == NULL) {
        if (value != NULL)
            istream_close_unused(value);

        request->callback.get(NULL, 0, NULL, request->callback_ctx);
        return;
    }

    sink_header_new(request->pool, value,
                    &http_cache_memcached_header_handler, request,
                    request->async_ref);
}

void
http_cache_memcached_get(pool_t pool, struct memcached_stock *stock,
                         pool_t background_pool,
                         struct background_manager *background,
                         const char *uri, struct strmap *request_headers,
                         http_cache_memcached_get_t callback,
                         void *callback_ctx,
                         struct async_operation_ref *async_ref)
{
    struct http_cache_memcached_request *request = p_malloc(pool, sizeof(*request));
    const char *key;

    request->pool = pool;
    request->stock = stock;
    request->background_pool = background_pool;
    request->background = background;
    request->uri = uri;
    request->request_headers = request_headers;
    request->in_choice = false;
    request->callback.get = callback;
    request->callback_ctx = callback_ctx;
    request->async_ref = async_ref;

    key = http_cache_choice_vary_key(pool, uri, NULL);

    memcached_stock_invoke(pool, stock, MEMCACHED_OPCODE_GET,
                           NULL, 0,
                           key, strlen(key),
                           NULL,
                           &http_cache_memcached_get_handler, request,
                           async_ref);
}

static void
mcd_choice_commit_callback(GError *error, void *ctx)
{
    struct http_cache_memcached_request *request = ctx;

    request->callback.put(error, request->callback_ctx);
}

static void
http_cache_memcached_put_response(enum memcached_response_status status,
                                  G_GNUC_UNUSED const void *extras,
                                  G_GNUC_UNUSED size_t extras_length,
                                  G_GNUC_UNUSED const void *key,
                                  G_GNUC_UNUSED size_t key_length,
                                  istream_t value, void *ctx)
{
    struct http_cache_memcached_request *request = ctx;

    if (value != NULL)
        istream_close_unused(value);

    if (status != MEMCACHED_STATUS_NO_ERROR || /* error */
        request->choice == NULL) { /* or no choice entry needed */
        request->callback.put(NULL, request->callback_ctx);
        return;
    }

    http_cache_choice_commit(request->choice, request->stock,
                             mcd_choice_commit_callback, request,
                             request->async_ref);
}

static void
http_cache_memcached_put_error(GError *error, void *ctx)
{
    struct http_cache_memcached_request *request = ctx;

    request->callback.put(error, request->callback_ctx);
}

static const struct memcached_client_handler http_cache_memcached_put_handler = {
    .response = http_cache_memcached_put_response,
    .error = http_cache_memcached_put_error,
};

void
http_cache_memcached_put(pool_t pool, struct memcached_stock *stock,
                         pool_t background_pool,
                         struct background_manager *background,
                         const char *uri,
                         const struct http_cache_info *info,
                         struct strmap *request_headers,
                         http_status_t status, struct strmap *response_headers,
                         istream_t value,
                         http_cache_memcached_put_t callback, void *callback_ctx,
                         struct async_operation_ref *async_ref)
{
    struct http_cache_memcached_request *request = p_malloc(pool, sizeof(*request));
    struct pool_mark mark;
    struct strmap *vary;
    struct growing_buffer *gb;
    const char *key;

    request->pool = pool;
    request->stock = stock;
    request->background_pool = background_pool;
    request->background = background;
    request->uri = uri;
    request->async_ref = async_ref;

    pool_mark(tpool, &mark);

    vary = info->vary != NULL
        ? http_cache_copy_vary(tpool, info->vary, request_headers)
        : NULL;

    request->choice = vary != NULL
        ? http_cache_choice_prepare(pool, uri, info, vary)
        : NULL;

    key = http_cache_choice_vary_key(pool, uri, vary);

    gb = growing_buffer_new(pool, 1024);

    /* type */
    serialize_uint32(gb, TYPE_DOCUMENT);

    serialize_uint64(gb, info->expires);
    serialize_strmap(gb, vary);

    /* serialize status + response headers */
    serialize_uint16(gb, status);
    serialize_strmap(gb, response_headers);

    request->header_size = g_htonl(growing_buffer_size(gb));

    /* append response body */
    value = istream_cat_new(pool,
                            istream_memory_new(pool, &request->header_size,
                                               sizeof(request->header_size)),
                            istream_gb_new(pool, gb), value, NULL);

    request->extras.set.flags = 0;
    request->extras.set.expiration = info->expires > 0
        ? g_htonl(info->expires) : g_htonl(3600);

    request->callback.put = callback;
    request->callback_ctx = callback_ctx;

    pool_rewind(tpool, &mark);

    memcached_stock_invoke(pool, stock,
                           MEMCACHED_OPCODE_SET,
                           &request->extras.set, sizeof(request->extras.set),
                           key, strlen(key),
                           value,
                           &http_cache_memcached_put_handler, request,
                           async_ref);
}

static void
mcd_background_response(G_GNUC_UNUSED enum memcached_response_status status,
                        G_GNUC_UNUSED const void *extras,
                        G_GNUC_UNUSED size_t extras_length,
                        G_GNUC_UNUSED const void *key,
                        G_GNUC_UNUSED size_t key_length,
                        istream_t value, void *ctx)
{
    struct background_job *job = ctx;

    if (value != NULL)
        istream_close_unused(value);

    background_manager_remove(job);
}

static void
mcd_background_error(GError *error, void *ctx)
{
    struct background_job *job = ctx;

    if (error != NULL) {
        cache_log(2, "http-cache: put failed: %s\n", error->message);
        g_error_free(error);
    }

    background_manager_remove(job);
}

static const struct memcached_client_handler mcd_background_handler = {
    .response = mcd_background_response,
    .error = mcd_background_error,
};

static void
mcd_background_delete(struct memcached_stock *stock,
                      pool_t background_pool,
                      struct background_manager *background,
                      const char *uri, struct strmap *vary)
{
    pool_t pool = pool_new_linear(background_pool,
                                  "http_cache_memcached_bkg_delete", 1024);
    struct background_job *job = p_malloc(pool, sizeof(*job));
    const char *key = http_cache_choice_vary_key(pool, uri, vary);

    memcached_stock_invoke(pool, stock,
                           MEMCACHED_OPCODE_DELETE,
                           NULL, 0,
                           key, strlen(key),
                           NULL,
                           &mcd_background_handler, job,
                           background_job_add(background, job));
    pool_unref(pool);
}

void
http_cache_memcached_remove_uri(struct memcached_stock *stock,
                                pool_t background_pool,
                                struct background_manager *background,
                                const char *uri)
{
    mcd_background_delete(stock, background_pool, background, uri, NULL);

    pool_t pool = pool_new_linear(background_pool,
                                  "http_cache_memcached_remove_uri", 8192);
    struct background_job *job = p_malloc(pool, sizeof(*job));
    http_cache_choice_delete(pool, stock, uri,
                             background_callback, job,
                             background_job_add(background, job));

    pool_unref(pool);
}

struct match_data {
    struct background_job job;

    struct memcached_stock *stock;
    pool_t background_pool;
    struct background_manager *background;
    const char *uri;
    struct strmap *headers;
};

static bool
mcd_delete_filter_callback(const struct http_cache_document *document,
                           GError *error, void *ctx)
{
    struct match_data *data = ctx;

    if (document != NULL) {
        /* discard documents matching the Vary specification */
        if (http_cache_document_fits(document, data->headers)) {
            mcd_background_delete(data->stock, data->background_pool,
                                  data->background, data->uri,
                                  document->vary);
            return false;
        } else
            return true;
    } else {
        if (error != NULL) {
            cache_log(2, "http-cache: memcached failed: %s\n", error->message);
            g_error_free(error);
        }

        background_manager_remove(&data->job);
        return false;
    }
}

void
http_cache_memcached_remove_uri_match(struct memcached_stock *stock,
                                      pool_t background_pool,
                                      struct background_manager *background,
                                      const char *uri, struct strmap *headers)
{
    /* delete the main document */
    mcd_background_delete(stock, background_pool, background, uri, NULL);

    pool_t pool = pool_new_linear(background_pool,
                                  "http_cache_memcached_remove_uri", 8192);

    /* now delete all matching Vary documents */
    struct match_data *data = p_malloc(pool, sizeof(*data));
    data->stock = stock;
    data->background_pool = background_pool;
    data->background = background;
    data->uri = p_strdup(pool, uri);
    data->headers = strmap_dup(pool, headers);

    http_cache_choice_filter(pool, stock, uri,
                             mcd_delete_filter_callback, data,
                             background_job_add(background, &data->job));

    pool_unref(pool);
}
