/*
 * Copyright 2007-2017 Content Management AG
 * All rights reserved.
 *
 * author: Max Kellermann <mk@cm4all.com>
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * - Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 *
 * - Redistributions in binary form must reproduce the above copyright
 * notice, this list of conditions and the following disclaimer in the
 * documentation and/or other materials provided with the
 * distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE
 * FOUNDATION OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED
 * OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "http_cache_memcached.hxx"
#include "http_cache_choice.hxx"
#include "http_cache_rfc.hxx"
#include "http_cache_document.hxx"
#include "http_cache_internal.hxx"
#include "memcached/memcached_stock.hxx"
#include "memcached/memcached_client.hxx"
#include "GrowingBuffer.hxx"
#include "serialize.hxx"
#include "strmap.hxx"
#include "tpool.hxx"
#include "istream_gb.hxx"
#include "istream/istream_cat.hxx"
#include "istream/istream_memory.hxx"
#include "istream/istream.hxx"
#include "istream/sink_header.hxx"
#include "istream/UnusedPtr.hxx"
#include "pool.hxx"
#include "io/Logger.hxx"
#include "util/Background.hxx"
#include "util/ByteOrder.hxx"
#include "util/ConstBuffer.hxx"

#include "util/Compiler.h"

#include <string.h>

enum http_cache_memcached_type {
    TYPE_DOCUMENT = 2,
};

struct HttpCacheMemcachedRequest {
    struct pool *pool;

    MemachedStock *stock;

    struct pool *background_pool;
    BackgroundManager *background;

    const char *uri;

    StringMap *request_headers;

    bool in_choice;
    HttpCacheChoice *choice;

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

    CancellablePointer *cancel_ptr;

    HttpCacheMemcachedRequest(struct pool &_pool)
        :pool(&_pool) {}

    HttpCacheMemcachedRequest(struct pool &_pool,
                              MemachedStock &_stock,
                              struct pool &_background_pool,
                              BackgroundManager &_background,
                              const char *_uri,
                              CancellablePointer &_cancel_ptr)
        :pool(&_pool), stock(&_stock),
         background_pool(&_background_pool), background(&_background),
         uri(_uri),
         cancel_ptr(&_cancel_ptr) {}

    HttpCacheMemcachedRequest(struct pool &_pool,
                              MemachedStock &_stock,
                              struct pool &_background_pool,
                              BackgroundManager &_background,
                              const char *_uri,
                              StringMap &_request_headers,
                              http_cache_memcached_get_t _callback,
                              void *_callback_ctx,
                              CancellablePointer &_cancel_ptr)
        :pool(&_pool), stock(&_stock),
         background_pool(&_background_pool), background(&_background),
         uri(_uri), request_headers(&_request_headers),
         in_choice(false),
         callback_ctx(_callback_ctx),
         cancel_ptr(&_cancel_ptr) {
        callback.get = _callback;
    }

    HttpCacheMemcachedRequest(const HttpCacheMemcachedRequest &) = delete;
    HttpCacheMemcachedRequest &operator=(const HttpCacheMemcachedRequest &) = delete;
};

/*
 * Public functions and memcached-client callbacks
 *
 */

static void
http_cache_memcached_flush_response(enum memcached_response_status status,
                                    gcc_unused const void *extras,
                                    gcc_unused size_t extras_length,
                                    gcc_unused const void *key,
                                    gcc_unused size_t key_length,
                                    UnusedIstreamPtr value, void *ctx)
{
    auto request = (HttpCacheMemcachedRequest *)ctx;

    value.Clear();

    request->callback.flush(status == MEMCACHED_STATUS_NO_ERROR,
                            nullptr, request->callback_ctx);
}

static void
http_cache_memcached_flush_error(std::exception_ptr ep, void *ctx)
{
    auto request = (HttpCacheMemcachedRequest *)ctx;

    request->callback.flush(false, ep, request->callback_ctx);
}

static const struct memcached_client_handler http_cache_memcached_flush_handler = {
    .response = http_cache_memcached_flush_response,
    .error = http_cache_memcached_flush_error,
};

void
http_cache_memcached_flush(struct pool &pool, MemachedStock &stock,
                           http_cache_memcached_flush_t callback,
                           void *callback_ctx,
                           CancellablePointer &cancel_ptr)
{
    auto request = NewFromPool<HttpCacheMemcachedRequest>(pool, pool);

    request->callback.flush = callback;
    request->callback_ctx = callback_ctx;

    memcached_stock_invoke(pool, stock, MEMCACHED_OPCODE_FLUSH,
                           nullptr, 0,
                           nullptr, 0,
                           nullptr,
                           http_cache_memcached_flush_handler, request,
                           cancel_ptr);
}

/**
 * Throws DeserializeError on error.
 */
static HttpCacheDocument *
mcd_deserialize_document(struct pool &pool, ConstBuffer<void> &header,
                         const StringMap *request_headers)
{
    auto document = NewFromPool<HttpCacheDocument>(pool, pool);

    document->info.expires =
        std::chrono::system_clock::from_time_t(deserialize_uint64(header));

    deserialize_strmap(header, document->vary);

    document->status = (http_status_t)deserialize_uint16(header);
    if (!http_status_is_valid(document->status))
        throw DeserializeError();

    deserialize_strmap(header, document->response_headers);

    document->info.last_modified =
        document->response_headers.Get("last-modified");
    document->info.etag =
        document->response_headers.Get("etag");
    document->info.vary =
        document->response_headers.Get("vary");

    if (!document->VaryFits(request_headers))
        /* Vary mismatch */
        return nullptr;

    return document;
}

static void
http_cache_memcached_get_response(enum memcached_response_status status,
                                  const void *extras, size_t extras_length,
                                  const void *key, size_t key_length,
                                  UnusedIstreamPtr value, void *ctx);

static void
http_cache_memcached_get_error(std::exception_ptr ep, void *ctx)
{
    auto request = (HttpCacheMemcachedRequest *)ctx;

    request->callback.get(nullptr, nullptr, ep, request->callback_ctx);
}

static const struct memcached_client_handler http_cache_memcached_get_handler = {
    .response = http_cache_memcached_get_response,
    .error = http_cache_memcached_get_error,
};

static void
background_callback(std::exception_ptr ep, void *ctx)
{
    LinkedBackgroundJob *job = (LinkedBackgroundJob *)ctx;

    if (ep)
        LogConcat(2, "HttpCache", "memcached failed: ", ep);

    job->Remove();
}

static void
mcd_choice_get_callback(const char *key, bool unclean,
                        std::exception_ptr ep, void *ctx)
{
    auto &request = *(HttpCacheMemcachedRequest *)ctx;

    if (unclean) {
        /* this choice record is unclean - start cleanup as a
           background job */
        struct pool *pool = pool_new_linear(request.background_pool,
                                      "http_cache_choice_cleanup", 8192);
        auto job = NewFromPool<LinkedBackgroundJob>(*pool,
                                                    *request.background);

        http_cache_choice_cleanup(*pool, *request.stock, request.uri,
                                  background_callback, job,
                                  request.background->Add2(*job));
        pool_unref(pool);
    }

    if (key == nullptr) {
        request.callback.get(nullptr, nullptr, ep, request.callback_ctx);
        return;
    }

    request.in_choice = true;
    memcached_stock_invoke(*request.pool, *request.stock, MEMCACHED_OPCODE_GET,
                           nullptr, 0,
                           key, strlen(key),
                           nullptr,
                           http_cache_memcached_get_handler, &request,
                           *request.cancel_ptr);
}

static void
http_cache_memcached_header_done(void *header_ptr, size_t length,
                                 UnusedIstreamPtr tail, void *ctx)
{
    auto &request = *(HttpCacheMemcachedRequest *)ctx;
    HttpCacheDocument *document;

    ConstBuffer<void> header(header_ptr, length);

    http_cache_memcached_type type;
    try {
        type = (http_cache_memcached_type)deserialize_uint32(header);
    } catch (DeserializeError) {
        tail.Clear();
        request.callback.get(nullptr, nullptr, nullptr, request.callback_ctx);
        return;
    }

    switch (type) {
    case TYPE_DOCUMENT:
        try {
            document = mcd_deserialize_document(*request.pool, header,
                                                request.request_headers);
        } catch (DeserializeError) {
            document = nullptr;
        }

        if (document == nullptr) {
            if (request.in_choice)
                break;

            tail.Clear();

            http_cache_choice_get(*request.pool, *request.stock,
                                  request.uri, request.request_headers,
                                  mcd_choice_get_callback, &request,
                                  *request.cancel_ptr);
            return;
        }

        request.callback.get(document, std::move(tail), nullptr,
                             request.callback_ctx);
        return;
    }

    tail.Clear();
    request.callback.get(nullptr, nullptr, nullptr, request.callback_ctx);
}

static void
http_cache_memcached_header_error(std::exception_ptr ep, void *ctx)
{
    auto &request = *(HttpCacheMemcachedRequest *)ctx;

    request.callback.get(nullptr, nullptr, ep, request.callback_ctx);
}

static const struct sink_header_handler http_cache_memcached_header_handler = {
    .done = http_cache_memcached_header_done,
    .error = http_cache_memcached_header_error,
};

static void
http_cache_memcached_get_response(enum memcached_response_status status,
                                  gcc_unused const void *extras,
                                  gcc_unused size_t extras_length,
                                  gcc_unused const void *key,
                                  gcc_unused size_t key_length,
                                  UnusedIstreamPtr value, void *ctx)
{
    auto &request = *(HttpCacheMemcachedRequest *)ctx;

    if (status == MEMCACHED_STATUS_KEY_NOT_FOUND && !request.in_choice) {
        value.Clear();
        http_cache_choice_get(*request.pool, *request.stock,
                              request.uri, request.request_headers,
                              mcd_choice_get_callback, &request,
                              *request.cancel_ptr);
        return;
    }

    if (status != MEMCACHED_STATUS_NO_ERROR || !value) {
        value.Clear();
        request.callback.get(nullptr, nullptr, nullptr, request.callback_ctx);
        return;
    }

    sink_header_new(*request.pool, std::move(value),
                    http_cache_memcached_header_handler, &request,
                    *request.cancel_ptr);
}

void
http_cache_memcached_get(struct pool &pool, MemachedStock &stock,
                         struct pool &background_pool,
                         BackgroundManager &background,
                         const char *uri, StringMap &request_headers,
                         http_cache_memcached_get_t callback,
                         void *callback_ctx,
                         CancellablePointer &cancel_ptr)
{
    auto request = NewFromPool<HttpCacheMemcachedRequest>(pool, pool,
                                                          stock,
                                                          background_pool,
                                                          background,
                                                          uri, request_headers,
                                                          callback, callback_ctx,
                                                          cancel_ptr);

    const char *key = http_cache_choice_vary_key(pool, uri, nullptr);

    memcached_stock_invoke(pool, stock, MEMCACHED_OPCODE_GET,
                           nullptr, 0,
                           key, strlen(key),
                           nullptr,
                           http_cache_memcached_get_handler, request,
                           cancel_ptr);
}

static void
mcd_choice_commit_callback(std::exception_ptr ep, void *ctx)
{
    auto &request = *(HttpCacheMemcachedRequest *)ctx;

    request.callback.put(ep, request.callback_ctx);
}

static void
http_cache_memcached_put_response(enum memcached_response_status status,
                                  gcc_unused const void *extras,
                                  gcc_unused size_t extras_length,
                                  gcc_unused const void *key,
                                  gcc_unused size_t key_length,
                                  UnusedIstreamPtr value, void *ctx)
{
    auto &request = *(HttpCacheMemcachedRequest *)ctx;

    value.Clear();

    if (status != MEMCACHED_STATUS_NO_ERROR || /* error */
        request.choice == nullptr) { /* or no choice entry needed */
        request.callback.put(nullptr, request.callback_ctx);
        return;
    }

    http_cache_choice_commit(*request.choice, *request.stock,
                             mcd_choice_commit_callback, &request,
                             *request.cancel_ptr);
}

static void
http_cache_memcached_put_error(std::exception_ptr ep, void *ctx)
{
    auto &request = *(HttpCacheMemcachedRequest *)ctx;

    request.callback.put(ep, request.callback_ctx);
}

static const struct memcached_client_handler http_cache_memcached_put_handler = {
    .response = http_cache_memcached_put_response,
    .error = http_cache_memcached_put_error,
};

void
http_cache_memcached_put(struct pool &pool, MemachedStock &stock,
                         struct pool &background_pool,
                         BackgroundManager &background,
                         const char *uri,
                         const HttpCacheResponseInfo &info,
                         const StringMap &request_headers,
                         http_status_t status,
                         const StringMap *response_headers,
                         UnusedIstreamPtr value,
                         http_cache_memcached_put_t callback, void *callback_ctx,
                         CancellablePointer &cancel_ptr)
{
    auto request = NewFromPool<HttpCacheMemcachedRequest>(pool, pool, stock,
                                                          background_pool,
                                                          background,
                                                          uri, cancel_ptr);

    const AutoRewindPool auto_rewind(*tpool);

    StringMap vary(*tpool);
    if (info.vary != nullptr)
        http_cache_copy_vary(vary, *tpool, info.vary, request_headers);

    request->choice = !vary.IsEmpty()
        ? http_cache_choice_prepare(pool, uri, info, vary)
        : nullptr;

    const char *key = http_cache_choice_vary_key(pool, uri, &vary);

    GrowingBuffer gb;

    /* type */
    serialize_uint32(gb, TYPE_DOCUMENT);

    serialize_uint64(gb, std::chrono::system_clock::to_time_t(info.expires));
    serialize_strmap(gb, vary);

    /* serialize status + response headers */
    serialize_uint16(gb, status);
    serialize_strmap(gb, response_headers);

    request->header_size = ToBE32(gb.GetSize());

    /* append response body */
    value = UnusedIstreamPtr(istream_cat_new(pool,
                                             istream_memory_new(pool, &request->header_size,
                                                                sizeof(request->header_size)).Steal(),
                                             istream_gb_new(pool, std::move(gb)),
                                             value.Steal()));

    request->extras.set.flags = 0;
    request->extras.set.expiration =
        ToBE32(info.expires > std::chrono::system_clock::from_time_t(0)
               ? std::chrono::system_clock::to_time_t(info.expires)
               : 3600);

    request->callback.put = callback;
    request->callback_ctx = callback_ctx;

    memcached_stock_invoke(pool, stock,
                           MEMCACHED_OPCODE_SET,
                           &request->extras.set, sizeof(request->extras.set),
                           key, strlen(key),
                           std::move(value),
                           http_cache_memcached_put_handler, request,
                           cancel_ptr);
}

static void
mcd_background_response(gcc_unused enum memcached_response_status status,
                        gcc_unused const void *extras,
                        gcc_unused size_t extras_length,
                        gcc_unused const void *key,
                        gcc_unused size_t key_length,
                        UnusedIstreamPtr value, void *ctx)
{
    LinkedBackgroundJob *job = (LinkedBackgroundJob *)ctx;

    value.Clear();

    job->Remove();
}

static void
mcd_background_error(std::exception_ptr ep, void *ctx)
{
    LinkedBackgroundJob *job = (LinkedBackgroundJob *)ctx;

    LogConcat(2, "HttpCache", "put failed: ", ep);

    job->Remove();
}

static const struct memcached_client_handler mcd_background_handler = {
    .response = mcd_background_response,
    .error = mcd_background_error,
};

static void
mcd_background_delete(MemachedStock &stock,
                      struct pool *background_pool,
                      BackgroundManager &background,
                      const char *uri, const StringMap *vary)
{
    struct pool *pool = pool_new_linear(background_pool,
                                        "http_cache_memcached_bkg_delete", 1024);
    auto job = NewFromPool<LinkedBackgroundJob>(*pool, background);
    const char *key = http_cache_choice_vary_key(*pool, uri, vary);

    memcached_stock_invoke(*pool, stock,
                           MEMCACHED_OPCODE_DELETE,
                           nullptr, 0,
                           key, strlen(key),
                           nullptr,
                           mcd_background_handler, job,
                           background.Add2(*job));
    pool_unref(pool);
}

void
http_cache_memcached_remove_uri(MemachedStock &stock,
                                struct pool &background_pool,
                                BackgroundManager &background,
                                const char *uri)
{
    mcd_background_delete(stock, &background_pool, background, uri, nullptr);

    struct pool *pool = pool_new_linear(&background_pool,
                                        "http_cache_memcached_remove_uri", 8192);
    auto job = NewFromPool<LinkedBackgroundJob>(*pool, background);
    http_cache_choice_delete(*pool, stock, uri,
                             background_callback, job,
                             background.Add2(*job));

    pool_unref(pool);
}

struct match_data {
    BackgroundJob job;

    MemachedStock *stock;
    struct pool *background_pool;
    BackgroundManager *background;
    const char *uri;
    StringMap *headers;
};

static bool
mcd_delete_filter_callback(const HttpCacheChoiceInfo *info,
                           std::exception_ptr ep, void *ctx)
{
    match_data *data = (match_data *)ctx;

    if (info != nullptr) {
        /* discard documents matching the Vary specification */
        if (info->VaryFits(data->headers)) {
            mcd_background_delete(*data->stock, data->background_pool,
                                  *data->background, data->uri,
                                  &info->vary);
            return false;
        } else
            return true;
    } else {
        if (ep)
            LogConcat(2, "HttpCache", "memcached failed: ", ep);

        data->background->Remove(data->job);
        return false;
    }
}

void
http_cache_memcached_remove_uri_match(MemachedStock &stock,
                                      struct pool &background_pool,
                                      BackgroundManager &background,
                                      const char *uri, StringMap &headers)
{
    /* delete the main document */
    mcd_background_delete(stock, &background_pool, background, uri, nullptr);

    struct pool *pool = pool_new_linear(&background_pool,
                                        "http_cache_memcached_remove_uri", 8192);

    /* now delete all matching Vary documents */
    auto data = NewFromPool<match_data>(*pool);
    data->stock = &stock;
    data->background_pool = &background_pool;
    data->background = &background;
    data->uri = p_strdup(pool, uri);
    data->headers = strmap_dup(pool, &headers);

    http_cache_choice_filter(*pool, stock, uri,
                             mcd_delete_filter_callback, data,
                             background.Add2(data->job));

    pool_unref(pool);
}
