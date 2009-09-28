/*
 * Caching HTTP responses.  Memcached choice backend.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "http-cache-choice.h"
#include "http-cache-internal.h"
#include "memcached-stock.h"
#include "tpool.h"
#include "format.h"
#include "strmap.h"
#include "strref.h"
#include "serialize.h"
#include "growing-buffer.h"
#include "sink-impl.h"
#include "uset.h"

#include <glib.h>

enum {
    CHOICE_MAGIC = 4,
};

struct http_cache_choice {
    pool_t pool;

    struct memcached_stock *stock;

    const char *uri;
    const char *key;

    const struct strmap *request_headers;

    struct strref data;

    struct memcached_set_extras extras;

    union {
        http_cache_choice_get_t get;
        http_cache_choice_commit_t commit;
        http_cache_choice_commit_t cleanup;
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

const char *
http_cache_choice_vary_key(pool_t pool, const char *uri, struct strmap *vary)
{
    char hash[9];
    format_uint32_hex_fixed(hash, mcd_vary_hash(vary));
    hash[8] = 0;

    return p_strcat(pool, uri, " ", hash, NULL);
}

static void
http_cache_choice_buffer_callback(void *data0, size_t length, void *ctx)
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

    if (data0 == NULL) {
        choice->callback.get(NULL, true, choice->callback_ctx);
        return;
    }

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

    choice->callback.get(uri, unclean, choice->callback_ctx);
}

static void
http_cache_choice_get_callback(enum memcached_response_status status,
                               G_GNUC_UNUSED const void *extras,
                               G_GNUC_UNUSED size_t extras_length,
                               G_GNUC_UNUSED const void *key,
                               G_GNUC_UNUSED size_t key_length,
                               istream_t value, void *ctx)
{
    struct http_cache_choice *choice = ctx;

    if (status != MEMCACHED_STATUS_NO_ERROR || value == NULL) {
        if (value != NULL)
            istream_close(value);

        choice->callback.get(NULL, false, choice->callback_ctx);
        return;
    }

    sink_buffer_new(choice->pool, value,
                    http_cache_choice_buffer_callback, choice,
                    choice->async_ref);
}

void
http_cache_choice_get(pool_t pool, struct memcached_stock *stock,
                      const char *uri, const struct strmap *request_headers,
                      http_cache_choice_get_t callback,
                      void *callback_ctx,
                      struct async_operation_ref *async_ref)
{
    struct http_cache_choice *choice = p_malloc(pool, sizeof(*choice));

    choice->pool = pool;
    choice->stock = stock;
    choice->uri = uri;
    choice->key = p_strcat(pool, uri, " choice", NULL);
    choice->request_headers = request_headers;
    choice->callback.get = callback;
    choice->callback_ctx = callback_ctx;
    choice->async_ref = async_ref;

    memcached_stock_invoke(pool, stock,
                           MEMCACHED_OPCODE_GET,
                           NULL, 0,
                           choice->key, strlen(choice->key),
                           NULL,
                           http_cache_choice_get_callback, choice,
                           async_ref);
}

struct http_cache_choice *
http_cache_choice_prepare(pool_t pool, const char *uri,
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
http_cache_choice_add_callback(G_GNUC_UNUSED enum memcached_response_status status,
                               G_GNUC_UNUSED const void *extras,
                               G_GNUC_UNUSED size_t extras_length,
                               G_GNUC_UNUSED const void *key,
                               G_GNUC_UNUSED size_t key_length,
                               istream_t value, void *ctx)
{
    struct http_cache_choice *choice = ctx;

    if (value != NULL)
        istream_close(value);

    choice->callback.commit(choice->callback_ctx);
}

static void
http_cache_choice_prepend_callback(enum memcached_response_status status,
                                   G_GNUC_UNUSED const void *extras,
                                   G_GNUC_UNUSED size_t extras_length,
                                   G_GNUC_UNUSED const void *key,
                                   G_GNUC_UNUSED size_t key_length,
                                   istream_t value, void *ctx)
{
    struct http_cache_choice *choice = ctx;

    if (value != NULL)
        istream_close(value);

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
                               http_cache_choice_add_callback, choice,
                               choice->async_ref);
        break;

    case MEMCACHED_STATUS_NO_ERROR:
    default:
        choice->callback.commit(choice->callback_ctx);
        break;
    }
}

void
http_cache_choice_commit(struct http_cache_choice *choice,
                         struct memcached_stock *stock,
                         http_cache_choice_commit_t callback,
                         void *callback_ctx,
                         struct async_operation_ref *async_ref)
{
    istream_t value;

    choice->key = p_strcat(choice->pool, choice->uri, " choice", NULL);
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
                           http_cache_choice_prepend_callback, choice,
                           async_ref);
}

static void
http_cache_choice_cleanup_set_callback(G_GNUC_UNUSED enum memcached_response_status status,
                                       G_GNUC_UNUSED const void *extras,
                                       G_GNUC_UNUSED size_t extras_length,
                                       G_GNUC_UNUSED const void *key,
                                       G_GNUC_UNUSED size_t key_length,
                                       G_GNUC_UNUSED istream_t value, void *ctx)
{
    struct http_cache_choice *choice = ctx;

    if (value != NULL)
        istream_close(value);

    choice->callback.cleanup(choice->callback_ctx);
}


static void
http_cache_choice_cleanup_buffer_callback(void *data0, size_t length,
                                          void *ctx)
{
    struct http_cache_choice *choice = ctx;
    struct strref data;
    const char *current;
    char *dest;
    time_t now = time(NULL);
    struct pool_mark mark;
    uint32_t magic;
    struct http_cache_document document;
    struct uset uset;
    unsigned hash;
    bool duplicate;

    if (data0 == NULL) {
        choice->callback.get(NULL, true, choice->callback_ctx);
        return;
    }

    strref_set(&data, data0, length);
    dest = data0;
    uset_init(&uset);

    while (!strref_is_empty(&data)) {
        current = data.data;

        magic = deserialize_uint32(&data);
        if (magic != CHOICE_MAGIC)
            break;

        document.info.expires = deserialize_uint64(&data);

        pool_mark(tpool, &mark);
        document.vary = deserialize_strmap(&data, tpool);
        hash = mcd_vary_hash(document.vary);
        pool_rewind(tpool, &mark);

        if (strref_is_null(&data))
            /* deserialization failure */
            break;

        duplicate = uset_contains_or_add(&uset, hash);
        if ((document.info.expires == -1 || document.info.expires >= now) &&
            !duplicate) {
            memmove(dest, current, strref_end(&data) - current);
            dest += data.data - current;
        }
    }

    if (dest - length == data0)
        /* no change */
        choice->callback.cleanup(choice->callback_ctx);
    else if (dest == data0)
        /* no entries left */
        /* XXX use CAS */
        memcached_stock_invoke(choice->pool, choice->stock,
                               MEMCACHED_OPCODE_DELETE,
                               NULL, 0,
                               choice->key, strlen(choice->key),
                               NULL,
                               http_cache_choice_cleanup_set_callback, choice,
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
                               http_cache_choice_cleanup_set_callback, choice,
                               choice->async_ref);
    }
}

static void
http_cache_choice_cleanup_get_callback(enum memcached_response_status status,
                                       G_GNUC_UNUSED const void *extras,
                                       G_GNUC_UNUSED size_t extras_length,
                                       G_GNUC_UNUSED const void *key,
                                       G_GNUC_UNUSED size_t key_length,
                                       istream_t value, void *ctx)
{
    struct http_cache_choice *choice = ctx;

    if (status != MEMCACHED_STATUS_NO_ERROR || value == NULL) {
        if (value != NULL)
            istream_close(value);

        choice->callback.cleanup(choice->callback_ctx);
        return;
    }

    sink_buffer_new(choice->pool, value,
                    http_cache_choice_cleanup_buffer_callback, choice,
                    choice->async_ref);
}

void
http_cache_choice_cleanup(pool_t pool, struct memcached_stock *stock,
                          const char *uri,
                          http_cache_choice_commit_t callback,
                          void *callback_ctx,
                          struct async_operation_ref *async_ref)
{
    struct http_cache_choice *choice = p_malloc(pool, sizeof(*choice));

    choice->pool = pool;
    choice->stock = stock;
    choice->uri = uri;
    choice->key = p_strcat(pool, uri, " choice", NULL);
    choice->callback.commit = callback;
    choice->callback_ctx = callback_ctx;
    choice->async_ref = async_ref;

    memcached_stock_invoke(pool, stock,
                           MEMCACHED_OPCODE_GET,
                           NULL, 0,
                           choice->key, strlen(choice->key),
                           NULL,
                           http_cache_choice_cleanup_get_callback, choice,
                           async_ref);
}

static void
http_cache_choice_delete_callback(G_GNUC_UNUSED enum memcached_response_status status,
                                  G_GNUC_UNUSED const void *extras,
                                  G_GNUC_UNUSED size_t extras_length,
                                  G_GNUC_UNUSED const void *key,
                                  G_GNUC_UNUSED size_t key_length,
                                  istream_t value, void *ctx)
{
    struct http_cache_choice *choice = ctx;

    if (value != NULL)
        istream_close(value);

    choice->callback.delete(choice->callback_ctx);
}

void
http_cache_choice_delete(pool_t pool, struct memcached_stock *stock,
                         const char *uri,
                         http_cache_choice_delete_t callback,
                         void *callback_ctx,
                         struct async_operation_ref *async_ref)
{
    struct http_cache_choice *choice = p_malloc(pool, sizeof(*choice));

    choice->pool = pool;
    choice->stock = stock;
    choice->uri = uri;
    choice->key = p_strcat(pool, uri, " choice", NULL);
    choice->callback.commit = callback;
    choice->callback_ctx = callback_ctx;
    choice->async_ref = async_ref;

    memcached_stock_invoke(pool, stock,
                           MEMCACHED_OPCODE_GET,
                           NULL, 0,
                           choice->key, strlen(choice->key),
                           NULL,
                           http_cache_choice_delete_callback, choice,
                           async_ref);
}
