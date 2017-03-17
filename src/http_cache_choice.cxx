/*
 * Caching HTTP responses.  Memcached choice backend.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "http_cache_choice.hxx"
#include "http_cache_rfc.hxx"
#include "http_cache_info.hxx"
#include "http_cache_internal.hxx"
#include "memcached/memcached_stock.hxx"
#include "memcached/memcached_client.hxx"
#include "tpool.hxx"
#include "format.h"
#include "strmap.hxx"
#include "serialize.hxx"
#include "GrowingBuffer.hxx"
#include "uset.hxx"
#include "istream/sink_buffer.hxx"
#include "istream/istream.hxx"
#include "istream/istream_memory.hxx"
#include "pool.hxx"
#include "util/djbhash.h"
#include "util/ConstBuffer.hxx"
#include "util/WritableBuffer.hxx"
#include "util/ByteOrder.hxx"

#include <inline/compiler.h>

#include <stdio.h>

enum {
    CHOICE_MAGIC = 4,
};

/**
 * Auto-abbreviate the input string by replacing a long trailer with
 * its MD5 sum.  This is a hack to allow storing long URIs as a
 * memcached key (250 bytes max).
 */
static const char *
maybe_abbreviate(const char *p)
{
    size_t length = strlen(p);
    if (length < 232)
        return p;

    static char buffer[256];
    char *checksum = g_compute_checksum_for_string(G_CHECKSUM_MD5, p + 200,
                                                   length - 200);
    snprintf(buffer, sizeof(buffer), "%.*s~%s", 200, p, checksum);
    g_free(checksum);
    return buffer;
}

static const char *
http_cache_choice_key(struct pool &pool, const char *uri)
{
    return p_strcat(&pool, maybe_abbreviate(uri), " choice", nullptr);
}

struct HttpCacheChoice {
    struct pool *const pool;

    MemachedStock *stock;

    const char *const uri;
    const char *key;

    const StringMap *request_headers;

    ConstBuffer<void> data;

    struct memcached_set_extras extras;

    union {
        http_cache_choice_get_t get;
        http_cache_choice_commit_t commit;
        http_cache_choice_filter_t filter;
        http_cache_choice_delete_t delete_;
    } callback;

    void *callback_ctx;

    CancellablePointer *cancel_ptr;

    HttpCacheChoice(struct pool &_pool, MemachedStock &_stock,
                    const char *_uri, const StringMap *_request_headers,
                    http_cache_choice_get_t _callback,
                    void *_callback_ctx,
                    CancellablePointer &_cancel_ptr)
        :pool(&_pool), stock(&_stock),
         uri(_uri), key(http_cache_choice_key(*pool, uri)),
         request_headers(_request_headers),
         callback_ctx(_callback_ctx),
         cancel_ptr(&_cancel_ptr) {
        callback.get = _callback;
    }

    HttpCacheChoice(struct pool &_pool, MemachedStock &_stock,
                    const char *_uri,
                    http_cache_choice_filter_t _callback,
                    void *_callback_ctx,
                    CancellablePointer &_cancel_ptr)
        :pool(&_pool), stock(&_stock),
         uri(_uri), key(http_cache_choice_key(*pool, uri)),
         callback_ctx(_callback_ctx),
         cancel_ptr(&_cancel_ptr) {
        callback.filter = _callback;
    }

    HttpCacheChoice(struct pool &_pool, MemachedStock &_stock,
                    const char *_uri,
                    http_cache_choice_delete_t _callback,
                    void *_callback_ctx,
                    CancellablePointer &_cancel_ptr)
        :pool(&_pool), stock(&_stock),
         uri(_uri), key(http_cache_choice_key(*pool, uri)),
         callback_ctx(_callback_ctx),
         cancel_ptr(&_cancel_ptr) {
        callback.delete_ = _callback;
    }

    HttpCacheChoice(struct pool &_pool, const char *_uri,
                    ConstBuffer<void> _data)
        :pool(&_pool),
         uri(_uri), data(_data) {}
};

bool
HttpCacheChoiceInfo::VaryFits(const StringMap *headers) const
{
    return http_cache_vary_fits(vary, headers);
}

/**
 * Calculate a aggregated hash value of the specified string map.
 * This is used as a suffix for the memcached
 */
static unsigned
mcd_vary_hash(const StringMap *vary)
{
    unsigned hash = 0;

    if (vary == nullptr)
        return 0;

    for (const auto &i : *vary)
        hash ^= djb_hash_string(i.key) ^ djb_hash_string(i.value);

    return hash;
}

const char *
http_cache_choice_vary_key(struct pool &pool, const char *uri,
                           const StringMap *vary)
{
    char hash[9];
    format_uint32_hex_fixed(hash, mcd_vary_hash(vary));
    hash[8] = 0;

    uri = maybe_abbreviate(uri);

    return p_strcat(&pool, uri, " ", hash, nullptr);
}

static void
http_cache_choice_buffer_done(void *data0, size_t length, void *ctx)
{
    auto choice = (HttpCacheChoice *)ctx;
    const auto now = std::chrono::system_clock::now();
    uint32_t magic;
    const char *uri = nullptr;
    bool unclean = false;
    struct uset uset;
    unsigned hash;

    ConstBuffer<void> data(data0, length);

    while (!data.IsEmpty()) {
        magic = deserialize_uint32(data);
        if (magic != CHOICE_MAGIC)
            break;

        const auto expires = std::chrono::system_clock::from_time_t(deserialize_uint64(data));

        const AutoRewindPool auto_rewind(*tpool);

        const StringMap *const vary = deserialize_strmap(data, *tpool);

        if (data.IsNull()) {
            /* deserialization failure */
            unclean = true;
            break;
        }

        hash = mcd_vary_hash(vary);
        if (hash != 0) {
            if (uset.ContainsOrInsert(hash))
                /* duplicate: mark the record as
                   "unclean", queue the garbage collector */
                unclean = true;
        }

        if (expires != std::chrono::system_clock::from_time_t(-1) &&
            expires < now)
            unclean = true;
        else if (uri == nullptr &&
                 http_cache_vary_fits(vary, choice->request_headers))
            uri = http_cache_choice_vary_key(*choice->pool, choice->uri, vary);

        if (uri != nullptr && unclean)
            /* we have already found something, and we think that this
               record is unclean - no point in parsing more, abort
               here */
            break;
    }

    choice->callback.get(uri, unclean, nullptr, choice->callback_ctx);
}

static void
http_cache_choice_buffer_error(GError *error, void *ctx)
{
    auto choice = (HttpCacheChoice *)ctx;

    choice->callback.get(nullptr, true, error, choice->callback_ctx);
}

static const struct sink_buffer_handler http_cache_choice_buffer_handler = {
    .done = http_cache_choice_buffer_done,
    .error = http_cache_choice_buffer_error,
};

static void
http_cache_choice_get_response(enum memcached_response_status status,
                               gcc_unused const void *extras,
                               gcc_unused size_t extras_length,
                               gcc_unused const void *key,
                               gcc_unused size_t key_length,
                               Istream *value, void *ctx)
{
    auto choice = (HttpCacheChoice *)ctx;

    if (status != MEMCACHED_STATUS_NO_ERROR || value == nullptr) {
        if (value != nullptr)
            value->CloseUnused();

        choice->callback.get(nullptr, false, nullptr, choice->callback_ctx);
        return;
    }

    sink_buffer_new(*choice->pool, *value,
                    http_cache_choice_buffer_handler, choice,
                    *choice->cancel_ptr);
}

static void
http_cache_choice_get_error(GError *error, void *ctx)
{
    auto choice = (HttpCacheChoice *)ctx;

    choice->callback.get(nullptr, false, error, choice->callback_ctx);
}

static const struct memcached_client_handler http_cache_choice_get_handler = {
    .response = http_cache_choice_get_response,
    .error = http_cache_choice_get_error,
};

void
http_cache_choice_get(struct pool &pool, MemachedStock &stock,
                      const char *uri, const StringMap *request_headers,
                      http_cache_choice_get_t callback,
                      void *callback_ctx,
                      CancellablePointer &cancel_ptr)
{
    auto choice = NewFromPool<HttpCacheChoice>(pool, pool, stock, uri,
                                               request_headers,
                                               callback, callback_ctx,
                                               cancel_ptr);

    memcached_stock_invoke(pool, stock,
                           MEMCACHED_OPCODE_GET,
                           nullptr, 0,
                           choice->key, strlen(choice->key),
                           nullptr,
                           http_cache_choice_get_handler, choice,
                           cancel_ptr);
}

HttpCacheChoice *
http_cache_choice_prepare(struct pool &pool, const char *uri,
                          const HttpCacheResponseInfo &info,
                          const StringMap &vary)
{
    GrowingBuffer gb;
    serialize_uint32(gb, CHOICE_MAGIC);
    serialize_uint64(gb, std::chrono::system_clock::to_time_t(info.expires));
    serialize_strmap(gb, vary);

    auto data = gb.Dup(pool);

    return NewFromPool<HttpCacheChoice>(pool, pool, uri,
                                        ConstBuffer<void>(data.data, data.size));
}

static void
http_cache_choice_add_response(gcc_unused enum memcached_response_status status,
                               gcc_unused const void *extras,
                               gcc_unused size_t extras_length,
                               gcc_unused const void *key,
                               gcc_unused size_t key_length,
                               Istream *value, void *ctx)
{
    auto choice = (HttpCacheChoice *)ctx;

    if (value != nullptr)
        value->CloseUnused();

    choice->callback.commit(nullptr, choice->callback_ctx);
}

static void
http_cache_choice_add_error(GError *error, void *ctx)
{
    auto choice = (HttpCacheChoice *)ctx;

    choice->callback.commit(error, choice->callback_ctx);
}

static const struct memcached_client_handler http_cache_choice_add_handler = {
    .response = http_cache_choice_add_response,
    .error = http_cache_choice_add_error,
};

static void
http_cache_choice_prepend_response(enum memcached_response_status status,
                                   gcc_unused const void *extras,
                                   gcc_unused size_t extras_length,
                                   gcc_unused const void *key,
                                   gcc_unused size_t key_length,
                                   Istream *value, void *ctx)
{
    auto choice = (HttpCacheChoice *)ctx;

    if (value != nullptr)
        value->CloseUnused();

    switch (status) {
    case MEMCACHED_STATUS_ITEM_NOT_STORED:
        /* could not prepend: try to add a new record */

        cache_log(5, "add '%s'\n", choice->key);

        choice->extras.flags = 0;
        choice->extras.expiration = ToBE32(600); /* XXX */

        value = istream_memory_new(choice->pool,
                                   choice->data.data, choice->data.size);
        memcached_stock_invoke(*choice->pool, *choice->stock,
                               MEMCACHED_OPCODE_ADD,
                               &choice->extras, sizeof(choice->extras),
                               choice->key, strlen(choice->key),
                               value,
                               http_cache_choice_add_handler, choice,
                               *choice->cancel_ptr);
        break;

    case MEMCACHED_STATUS_NO_ERROR:
    default:
        choice->callback.commit(nullptr, choice->callback_ctx);
        break;
    }
}

static void
http_cache_choice_prepend_error(GError *error, void *ctx)
{
    auto choice = (HttpCacheChoice *)ctx;

    choice->callback.commit(error, choice->callback_ctx);
}

static const struct memcached_client_handler http_cache_choice_prepend_handler = {
    .response = http_cache_choice_prepend_response,
    .error = http_cache_choice_prepend_error,
};

void
http_cache_choice_commit(HttpCacheChoice &choice,
                         MemachedStock &stock,
                         http_cache_choice_commit_t callback,
                         void *callback_ctx,
                         CancellablePointer &cancel_ptr)
{
    choice.key = http_cache_choice_key(*choice.pool, choice.uri);
    choice.stock = &stock;
    choice.callback.commit = callback;
    choice.callback_ctx = callback_ctx;
    choice.cancel_ptr = &cancel_ptr;

    cache_log(5, "prepend '%s'\n", choice.key);

    Istream *value = istream_memory_new(choice.pool,
                                        choice.data.data,
                                        choice.data.size);
    memcached_stock_invoke(*choice.pool, stock,
                           MEMCACHED_OPCODE_PREPEND,
                           nullptr, 0,
                           choice.key, strlen(choice.key), value,
                           http_cache_choice_prepend_handler, &choice,
                           cancel_ptr);
}

static void
http_cache_choice_filter_set_response(gcc_unused enum memcached_response_status status,
                                      gcc_unused const void *extras,
                                      gcc_unused size_t extras_length,
                                      gcc_unused const void *key,
                                      gcc_unused size_t key_length,
                                      gcc_unused Istream *value, void *ctx)
{
    auto choice = (HttpCacheChoice *)ctx;

    if (value != nullptr)
        value->CloseUnused();

    choice->callback.filter(nullptr, nullptr, choice->callback_ctx);
}

static void
http_cache_choice_filter_set_error(GError *error, void *ctx)
{
    auto choice = (HttpCacheChoice *)ctx;

    choice->callback.filter(nullptr, error, choice->callback_ctx);
}

static const struct memcached_client_handler http_cache_choice_filter_set_handler = {
    .response = http_cache_choice_filter_set_response,
    .error = http_cache_choice_filter_set_error,
};

static void
http_cache_choice_filter_buffer_done(void *data0, size_t length, void *ctx)
{
    auto choice = (HttpCacheChoice *)ctx;

    ConstBuffer<void> data(data0, length);
    char *dest = (char *)data0;

    while (!data.IsEmpty()) {
        const void *current = data.data;

        const uint32_t magic = deserialize_uint32(data);
        if (magic != CHOICE_MAGIC)
            break;

        HttpCacheChoiceInfo info;
        info.expires = std::chrono::system_clock::from_time_t(deserialize_uint64(data));

        const AutoRewindPool auto_rewind(*tpool);
        info.vary = deserialize_strmap(data, *tpool);

        if (data.IsNull())
            /* deserialization failure */
            break;

        if (choice->callback.filter(&info, nullptr, choice->callback_ctx)) {
            memmove(dest, current, (const uint8_t *)data.data + data.size - (const uint8_t *)current);
            dest += (const uint8_t *)data.data - (const uint8_t *)current;
        }
    }

    if (dest - length == data0)
        /* no change */
        choice->callback.filter(nullptr, nullptr, choice->callback_ctx);
    else if (dest == data0)
        /* no entries left */
        /* XXX use CAS */
        memcached_stock_invoke(*choice->pool, *choice->stock,
                               MEMCACHED_OPCODE_DELETE,
                               nullptr, 0,
                               choice->key, strlen(choice->key),
                               nullptr,
                               http_cache_choice_filter_set_handler, choice,
                               *choice->cancel_ptr);
    else {
        /* send new contents */
        /* XXX use CAS */

        choice->extras.flags = 0;
        choice->extras.expiration = ToBE32(600); /* XXX */

        memcached_stock_invoke(*choice->pool, *choice->stock,
                               MEMCACHED_OPCODE_REPLACE,
                               &choice->extras, sizeof(choice->extras),
                               choice->key, strlen(choice->key),
                               istream_memory_new(choice->pool, data0,
                                                  dest - (char *)data0),
                               http_cache_choice_filter_set_handler, choice,
                               *choice->cancel_ptr);
    }
}

static void
http_cache_choice_filter_buffer_error(GError *error, void *ctx)
{
    auto choice = (HttpCacheChoice *)ctx;

    choice->callback.filter(nullptr, error, choice->callback_ctx);
}

static const struct sink_buffer_handler http_cache_choice_filter_buffer_handler = {
    .done = http_cache_choice_filter_buffer_done,
    .error = http_cache_choice_filter_buffer_error,
};

static void
http_cache_choice_filter_get_response(enum memcached_response_status status,
                                      gcc_unused const void *extras,
                                      gcc_unused size_t extras_length,
                                      gcc_unused const void *key,
                                      gcc_unused size_t key_length,
                                      Istream *value, void *ctx)
{
    auto choice = (HttpCacheChoice *)ctx;

    if (status != MEMCACHED_STATUS_NO_ERROR || value == nullptr) {
        if (value != nullptr)
            value->CloseUnused();

        choice->callback.filter(nullptr, nullptr, choice->callback_ctx);
        return;
    }

    sink_buffer_new(*choice->pool, *value,
                    http_cache_choice_filter_buffer_handler, choice,
                    *choice->cancel_ptr);
}

static void
http_cache_choice_filter_get_error(GError *error, void *ctx)
{
    auto choice = (HttpCacheChoice *)ctx;

    choice->callback.filter(nullptr, error, choice->callback_ctx);
}

static const struct memcached_client_handler http_cache_choice_filter_get_handler = {
    .response = http_cache_choice_filter_get_response,
    .error = http_cache_choice_filter_get_error,
};

void
http_cache_choice_filter(struct pool &pool, MemachedStock &stock,
                         const char *uri,
                         http_cache_choice_filter_t callback,
                         void *callback_ctx,
                         CancellablePointer &cancel_ptr)
{
    auto choice = NewFromPool<HttpCacheChoice>(pool, pool, stock, uri,
                                               callback, callback_ctx,
                                               cancel_ptr);

    memcached_stock_invoke(pool, stock,
                           MEMCACHED_OPCODE_GET,
                           nullptr, 0,
                           choice->key, strlen(choice->key),
                           nullptr,
                           http_cache_choice_filter_get_handler, choice,
                           cancel_ptr);
}

struct HttpCacheChoiceCleanup {
    const std::chrono::system_clock::time_point now =
        std::chrono::system_clock::now();

    struct uset uset;

    const http_cache_choice_cleanup_t callback;
    void *const callback_ctx;

    HttpCacheChoiceCleanup(http_cache_choice_cleanup_t _callback,
                           void *_callback_ctx)
        :callback(_callback), callback_ctx(_callback_ctx) {
    }
};

static bool
http_cache_choice_cleanup_filter_callback(const HttpCacheChoiceInfo *info,
                                          GError *error, void *ctx)
{
    auto &cleanup = *(HttpCacheChoiceCleanup *)ctx;

    if (info != nullptr) {
        unsigned hash = mcd_vary_hash(info->vary);
        bool duplicate = cleanup.uset.ContainsOrInsert(hash);
        return (info->expires == std::chrono::system_clock::from_time_t(-1) ||
                info->expires >= cleanup.now) &&
            !duplicate;
    } else {
        cleanup.callback(error, cleanup.callback_ctx);
        return false;
    }
}

void
http_cache_choice_cleanup(struct pool &pool, MemachedStock &stock,
                          const char *uri,
                          http_cache_choice_cleanup_t callback,
                          void *callback_ctx,
                          CancellablePointer &cancel_ptr)
{
    auto cleanup = NewFromPool<HttpCacheChoiceCleanup>(pool, callback,
                                                       callback_ctx);

    http_cache_choice_filter(pool, stock, uri,
                             http_cache_choice_cleanup_filter_callback, cleanup,
                             cancel_ptr);
}

static void
http_cache_choice_delete_response(gcc_unused enum memcached_response_status status,
                                  gcc_unused const void *extras,
                                  gcc_unused size_t extras_length,
                                  gcc_unused const void *key,
                                  gcc_unused size_t key_length,
                                  Istream *value, void *ctx)
{
    auto choice = (HttpCacheChoice *)ctx;

    if (value != nullptr)
        value->CloseUnused();

    choice->callback.delete_(nullptr, choice->callback_ctx);
}

static void
http_cache_choice_delete_error(GError *error, void *ctx)
{
    auto choice = (HttpCacheChoice *)ctx;

    choice->callback.delete_(error, choice->callback_ctx);
}

static const struct memcached_client_handler http_cache_choice_delete_handler = {
    .response = http_cache_choice_delete_response,
    .error = http_cache_choice_delete_error,
};

void
http_cache_choice_delete(struct pool &pool, MemachedStock &stock,
                         const char *uri,
                         http_cache_choice_delete_t callback,
                         void *callback_ctx,
                         CancellablePointer &cancel_ptr)
{
    auto choice = NewFromPool<HttpCacheChoice>(pool, pool, stock, uri,
                                               callback, callback_ctx,
                                               cancel_ptr);

    memcached_stock_invoke(pool, stock,
                           MEMCACHED_OPCODE_GET,
                           nullptr, 0,
                           choice->key, strlen(choice->key),
                           nullptr,
                           http_cache_choice_delete_handler, choice,
                           cancel_ptr);
}
