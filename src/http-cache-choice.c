/*
 * Caching HTTP responses.  Memcached choice backend.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "http-cache-choice.h"
#include "http-cache-internal.h"
#include "memcached-stock.h"
#include "memcached-client.h"
#include "tpool.h"
#include "format.h"
#include "strmap.h"
#include "strref.h"
#include "serialize.h"
#include "growing-buffer.h"
#include "sink-buffer.h"
#include "uset.h"
#include "istream.h"

#include <glib.h>
#include <stdio.h>

enum {
    CHOICE_MAGIC = 4,
};

struct http_cache_choice {
    struct pool *pool;

    struct memcached_stock *stock;

    const char *uri;
    const char *key;

    const struct strmap *request_headers;

    struct strref data;

    struct memcached_set_extras extras;

    union {
        http_cache_choice_get_t get;
        http_cache_choice_commit_t commit;
        http_cache_choice_filter_t filter;
        http_cache_choice_delete_t delete;
    } callback;

    void *callback_ctx;

    struct async_operation_ref *async_ref;
};

static inline unsigned
calc_hash(const char *p) {
    unsigned hash = 5381;

    assert(p != NULL);

    while (*p != 0)
        hash = (hash << 5) + hash + *p++;

    return hash;
}

/**
 * Calculate a aggregated hash value of the specified string map.
 * This is used as a suffix for the memcached
 */
static unsigned
mcd_vary_hash(struct strmap *vary)
{
    unsigned hash = 0;
    const struct strmap_pair *pair;

    if (vary == NULL)
        return 0;

    strmap_rewind(vary);

    while ((pair = strmap_next(vary)) != NULL)
        hash ^= calc_hash(pair->key) ^ calc_hash(pair->value);

    return hash;
}

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

const char *
http_cache_choice_vary_key(struct pool *pool, const char *uri, struct strmap *vary)
{
    char hash[9];
    format_uint32_hex_fixed(hash, mcd_vary_hash(vary));
    hash[8] = 0;

    uri = maybe_abbreviate(uri);

    return p_strcat(pool, uri, " ", hash, NULL);
}

static const char *
http_cache_choice_key(struct pool *pool, const char *uri)
{
    return p_strcat(pool, maybe_abbreviate(uri), " choice", NULL);
}

static void
http_cache_choice_buffer_done(void *data0, size_t length, void *ctx)
{
    struct http_cache_choice *choice = ctx;
    struct strref data;
    time_t now = time(NULL);
    struct pool_mark mark;
    uint32_t magic;
    struct http_cache_document document;
    const char *uri = NULL;
    bool unclean = false;
    struct uset uset;
    unsigned hash;

    strref_set(&data, data0, length);
    uset_init(&uset);

    while (!strref_is_empty(&data)) {
        magic = deserialize_uint32(&data);
        if (magic != CHOICE_MAGIC)
            break;

        document.info.expires = deserialize_uint64(&data);

        pool_mark(tpool, &mark);
        document.vary = deserialize_strmap(&data, tpool);

        if (strref_is_null(&data)) {
            /* deserialization failure */
            pool_rewind(tpool, &mark);
            unclean = true;
            break;
        }

        hash = mcd_vary_hash(document.vary);
        if (hash != 0) {
            if (uset_contains_or_add(&uset, hash))
                /* duplicate: mark the record as
                   "unclean", queue the garbage collector */
                unclean = true;
        }

        if (document.info.expires != -1 && document.info.expires < now)
            unclean = true;
        else if (uri == NULL &&
                 http_cache_document_fits(&document,
                                          choice->request_headers))
            uri = http_cache_choice_vary_key(choice->pool, choice->uri,
                                             document.vary);

        pool_rewind(tpool, &mark);

        if (uri != NULL && unclean)
            /* we have already found something, and we think that this
               record is unclean - no point in parsing more, abort
               here */
            break;
    }

    choice->callback.get(uri, unclean, NULL, choice->callback_ctx);
}

static void
http_cache_choice_buffer_error(GError *error, void *ctx)
{
    struct http_cache_choice *choice = ctx;

    choice->callback.get(NULL, true, error, choice->callback_ctx);
}

static const struct sink_buffer_handler http_cache_choice_buffer_handler = {
    .done = http_cache_choice_buffer_done,
    .error = http_cache_choice_buffer_error,
};

static void
http_cache_choice_get_response(enum memcached_response_status status,
                               G_GNUC_UNUSED const void *extras,
                               G_GNUC_UNUSED size_t extras_length,
                               G_GNUC_UNUSED const void *key,
                               G_GNUC_UNUSED size_t key_length,
                               struct istream *value, void *ctx)
{
    struct http_cache_choice *choice = ctx;

    if (status != MEMCACHED_STATUS_NO_ERROR || value == NULL) {
        if (value != NULL)
            istream_close_unused(value);

        choice->callback.get(NULL, false, NULL, choice->callback_ctx);
        return;
    }

    sink_buffer_new(choice->pool, value,
                    &http_cache_choice_buffer_handler, choice,
                    choice->async_ref);
}

static void
http_cache_choice_get_error(GError *error, void *ctx)
{
    struct http_cache_choice *choice = ctx;

    choice->callback.get(NULL, false, error, choice->callback_ctx);
}

static const struct memcached_client_handler http_cache_choice_get_handler = {
    .response = http_cache_choice_get_response,
    .error = http_cache_choice_get_error,
};

void
http_cache_choice_get(struct pool *pool, struct memcached_stock *stock,
                      const char *uri, const struct strmap *request_headers,
                      http_cache_choice_get_t callback,
                      void *callback_ctx,
                      struct async_operation_ref *async_ref)
{
    struct http_cache_choice *choice = p_malloc(pool, sizeof(*choice));

    choice->pool = pool;
    choice->stock = stock;
    choice->uri = uri;
    choice->key = http_cache_choice_key(pool, uri);
    choice->request_headers = request_headers;
    choice->callback.get = callback;
    choice->callback_ctx = callback_ctx;
    choice->async_ref = async_ref;

    memcached_stock_invoke(pool, stock,
                           MEMCACHED_OPCODE_GET,
                           NULL, 0,
                           choice->key, strlen(choice->key),
                           NULL,
                           &http_cache_choice_get_handler, choice,
                           async_ref);
}

struct http_cache_choice *
http_cache_choice_prepare(struct pool *pool, const char *uri,
                          const struct http_cache_info *info,
                          struct strmap *vary)
{
    struct http_cache_choice *choice = p_malloc(pool, sizeof(*choice));
    struct growing_buffer *gb;

    choice->pool = pool;
    choice->uri = uri;

    gb = growing_buffer_new(tpool, 1024);
    serialize_uint32(gb, CHOICE_MAGIC);
    serialize_uint64(gb, info->expires);
    serialize_strmap(gb, vary);

    choice->data.data = growing_buffer_dup(gb, pool, &choice->data.length);

    return choice;
}

static void
http_cache_choice_add_response(G_GNUC_UNUSED enum memcached_response_status status,
                               G_GNUC_UNUSED const void *extras,
                               G_GNUC_UNUSED size_t extras_length,
                               G_GNUC_UNUSED const void *key,
                               G_GNUC_UNUSED size_t key_length,
                               struct istream *value, void *ctx)
{
    struct http_cache_choice *choice = ctx;

    if (value != NULL)
        istream_close_unused(value);

    choice->callback.commit(NULL, choice->callback_ctx);
}

static void
http_cache_choice_add_error(GError *error, void *ctx)
{
    struct http_cache_choice *choice = ctx;

    choice->callback.commit(error, choice->callback_ctx);
}

static const struct memcached_client_handler http_cache_choice_add_handler = {
    .response = http_cache_choice_add_response,
    .error = http_cache_choice_add_error,
};

static void
http_cache_choice_prepend_response(enum memcached_response_status status,
                                   G_GNUC_UNUSED const void *extras,
                                   G_GNUC_UNUSED size_t extras_length,
                                   G_GNUC_UNUSED const void *key,
                                   G_GNUC_UNUSED size_t key_length,
                                   struct istream *value, void *ctx)
{
    struct http_cache_choice *choice = ctx;

    if (value != NULL)
        istream_close_unused(value);

    switch (status) {
    case MEMCACHED_STATUS_ITEM_NOT_STORED:
        /* could not prepend: try to add a new record */

        cache_log(5, "add '%s'\n", choice->key);

        choice->extras.flags = 0;
        choice->extras.expiration = g_htonl(600); /* XXX */

        value = istream_memory_new(choice->pool,
                                   choice->data.data, choice->data.length);
        memcached_stock_invoke(choice->pool, choice->stock,
                               MEMCACHED_OPCODE_ADD,
                               &choice->extras, sizeof(choice->extras),
                               choice->key, strlen(choice->key),
                               value,
                               &http_cache_choice_add_handler, choice,
                               choice->async_ref);
        break;

    case MEMCACHED_STATUS_NO_ERROR:
    default:
        choice->callback.commit(NULL, choice->callback_ctx);
        break;
    }
}

static void
http_cache_choice_prepend_error(GError *error, void *ctx)
{
    struct http_cache_choice *choice = ctx;

    choice->callback.commit(error, choice->callback_ctx);
}

static const struct memcached_client_handler http_cache_choice_prepend_handler = {
    .response = http_cache_choice_prepend_response,
    .error = http_cache_choice_prepend_error,
};

void
http_cache_choice_commit(struct http_cache_choice *choice,
                         struct memcached_stock *stock,
                         http_cache_choice_commit_t callback,
                         void *callback_ctx,
                         struct async_operation_ref *async_ref)
{
    struct istream *value;

    choice->key = http_cache_choice_key(choice->pool, choice->uri);
    choice->stock = stock;
    choice->callback.commit = callback;
    choice->callback_ctx = callback_ctx;
    choice->async_ref = async_ref;

    cache_log(5, "prepend '%s'\n", choice->key);

    value = istream_memory_new(choice->pool,
                               choice->data.data, choice->data.length);
    memcached_stock_invoke(choice->pool, stock,
                           MEMCACHED_OPCODE_PREPEND,
                           NULL, 0,
                           choice->key, strlen(choice->key), value,
                           &http_cache_choice_prepend_handler, choice,
                           async_ref);
}

static void
http_cache_choice_filter_set_response(G_GNUC_UNUSED enum memcached_response_status status,
                                       G_GNUC_UNUSED const void *extras,
                                       G_GNUC_UNUSED size_t extras_length,
                                       G_GNUC_UNUSED const void *key,
                                       G_GNUC_UNUSED size_t key_length,
                                       G_GNUC_UNUSED struct istream *value, void *ctx)
{
    struct http_cache_choice *choice = ctx;

    if (value != NULL)
        istream_close_unused(value);

    choice->callback.filter(NULL, NULL, choice->callback_ctx);
}

static void
http_cache_choice_filter_set_error(GError *error, void *ctx)
{
    struct http_cache_choice *choice = ctx;

    choice->callback.filter(NULL, error, choice->callback_ctx);
}

static const struct memcached_client_handler http_cache_choice_filter_set_handler = {
    .response = http_cache_choice_filter_set_response,
    .error = http_cache_choice_filter_set_error,
};

static void
http_cache_choice_filter_buffer_done(void *data0, size_t length, void *ctx)
{
    struct http_cache_choice *choice = ctx;
    struct strref data;
    const char *current;
    char *dest;
    struct pool_mark mark;
    uint32_t magic;
    struct http_cache_document document;

    strref_set(&data, data0, length);
    dest = data0;

    while (!strref_is_empty(&data)) {
        current = data.data;

        magic = deserialize_uint32(&data);
        if (magic != CHOICE_MAGIC)
            break;

        document.info.expires = deserialize_uint64(&data);

        pool_mark(tpool, &mark);
        document.vary = deserialize_strmap(&data, tpool);

        if (strref_is_null(&data)) {
            /* deserialization failure */
            pool_rewind(tpool, &mark);
            break;
        }

        if (choice->callback.filter(&document, NULL, choice->callback_ctx)) {
            memmove(dest, current, strref_end(&data) - current);
            dest += data.data - current;
        }

        pool_rewind(tpool, &mark);
    }

    if (dest - length == data0)
        /* no change */
        choice->callback.filter(NULL, NULL, choice->callback_ctx);
    else if (dest == data0)
        /* no entries left */
        /* XXX use CAS */
        memcached_stock_invoke(choice->pool, choice->stock,
                               MEMCACHED_OPCODE_DELETE,
                               NULL, 0,
                               choice->key, strlen(choice->key),
                               NULL,
                               &http_cache_choice_filter_set_handler, choice,
                               choice->async_ref);
    else {
        /* send new contents */
        /* XXX use CAS */

        choice->extras.flags = 0;
        choice->extras.expiration = g_htonl(600); /* XXX */

        memcached_stock_invoke(choice->pool, choice->stock,
                               MEMCACHED_OPCODE_REPLACE,
                               &choice->extras, sizeof(choice->extras),
                               choice->key, strlen(choice->key),
                               istream_memory_new(choice->pool, data0,
                                                  dest - (char *)data0),
                               &http_cache_choice_filter_set_handler, choice,
                               choice->async_ref);
    }
}

static void
http_cache_choice_filter_buffer_error(GError *error, void *ctx)
{
    struct http_cache_choice *choice = ctx;

    choice->callback.filter(NULL, error, choice->callback_ctx);
}

static const struct sink_buffer_handler http_cache_choice_filter_buffer_handler = {
    .done = http_cache_choice_filter_buffer_done,
    .error = http_cache_choice_filter_buffer_error,
};

static void
http_cache_choice_filter_get_response(enum memcached_response_status status,
                                      G_GNUC_UNUSED const void *extras,
                                      G_GNUC_UNUSED size_t extras_length,
                                      G_GNUC_UNUSED const void *key,
                                      G_GNUC_UNUSED size_t key_length,
                                      struct istream *value, void *ctx)
{
    struct http_cache_choice *choice = ctx;

    if (status != MEMCACHED_STATUS_NO_ERROR || value == NULL) {
        if (value != NULL)
            istream_close_unused(value);

        choice->callback.filter(NULL, NULL, choice->callback_ctx);
        return;
    }

    sink_buffer_new(choice->pool, value,
                    &http_cache_choice_filter_buffer_handler, choice,
                    choice->async_ref);
}

static void
http_cache_choice_filter_get_error(GError *error, void *ctx)
{
    struct http_cache_choice *choice = ctx;

    choice->callback.filter(NULL, error, choice->callback_ctx);
}

static const struct memcached_client_handler http_cache_choice_filter_get_handler = {
    .response = http_cache_choice_filter_get_response,
    .error = http_cache_choice_filter_get_error,
};

void
http_cache_choice_filter(struct pool *pool, struct memcached_stock *stock,
                         const char *uri,
                         http_cache_choice_filter_t callback,
                         void *callback_ctx,
                         struct async_operation_ref *async_ref)
{
    struct http_cache_choice *choice = p_malloc(pool, sizeof(*choice));

    choice->pool = pool;
    choice->stock = stock;
    choice->uri = uri;
    choice->key = http_cache_choice_key(pool, uri);
    choice->callback.filter = callback;
    choice->callback_ctx = callback_ctx;
    choice->async_ref = async_ref;

    memcached_stock_invoke(pool, stock,
                           MEMCACHED_OPCODE_GET,
                           NULL, 0,
                           choice->key, strlen(choice->key),
                           NULL,
                           &http_cache_choice_filter_get_handler, choice,
                           async_ref);
}

struct cleanup_data {
    time_t now;
    struct uset uset;

    http_cache_choice_cleanup_t callback;
    void *callback_ctx;
};

static bool
http_cache_choice_cleanup_filter_callback(const struct http_cache_document *document,
                                          GError *error, void *ctx)
{
    struct cleanup_data *data = ctx;

    if (document != NULL) {
        unsigned hash = mcd_vary_hash(document->vary);
        bool duplicate = uset_contains_or_add(&data->uset, hash);
        return (document->info.expires == -1 ||
                document->info.expires >= data->now) &&
            !duplicate;
    } else {
        data->callback(error, data->callback_ctx);
        return false;
    }
}

void
http_cache_choice_cleanup(struct pool *pool, struct memcached_stock *stock,
                          const char *uri,
                          http_cache_choice_cleanup_t callback,
                          void *callback_ctx,
                          struct async_operation_ref *async_ref)
{
    struct cleanup_data *data = p_malloc(pool, sizeof(*data));

    data->now = time(NULL);
    uset_init(&data->uset);
    data->callback = callback;
    data->callback_ctx = callback_ctx;

    http_cache_choice_filter(pool, stock, uri,
                             http_cache_choice_cleanup_filter_callback, data,
                             async_ref);
}

static void
http_cache_choice_delete_response(G_GNUC_UNUSED enum memcached_response_status status,
                                  G_GNUC_UNUSED const void *extras,
                                  G_GNUC_UNUSED size_t extras_length,
                                  G_GNUC_UNUSED const void *key,
                                  G_GNUC_UNUSED size_t key_length,
                                  struct istream *value, void *ctx)
{
    struct http_cache_choice *choice = ctx;

    if (value != NULL)
        istream_close_unused(value);

    choice->callback.delete(NULL, choice->callback_ctx);
}

static void
http_cache_choice_delete_error(GError *error, void *ctx)
{
    struct http_cache_choice *choice = ctx;

    choice->callback.delete(error, choice->callback_ctx);
}

static const struct memcached_client_handler http_cache_choice_delete_handler = {
    .response = http_cache_choice_delete_response,
    .error = http_cache_choice_delete_error,
};

void
http_cache_choice_delete(struct pool *pool, struct memcached_stock *stock,
                         const char *uri,
                         http_cache_choice_delete_t callback,
                         void *callback_ctx,
                         struct async_operation_ref *async_ref)
{
    struct http_cache_choice *choice = p_malloc(pool, sizeof(*choice));

    choice->pool = pool;
    choice->stock = stock;
    choice->uri = uri;
    choice->key = http_cache_choice_key(pool, uri);
    choice->callback.delete = callback;
    choice->callback_ctx = callback_ctx;
    choice->async_ref = async_ref;

    memcached_stock_invoke(pool, stock,
                           MEMCACHED_OPCODE_GET,
                           NULL, 0,
                           choice->key, strlen(choice->key),
                           NULL,
                           &http_cache_choice_delete_handler, choice,
                           async_ref);
}
