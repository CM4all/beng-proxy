/*
 * Cache for translation server responses.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "tcache.h"
#include "tstock.h"
#include "transformation.h"
#include "cache.h"
#include "stock.h"
#include "strmap.h"
#include "uri-address.h"
#include "beng-proxy/translation.h"

#include <time.h>
#include <string.h>

struct tcache_item {
    struct cache_item item;

    pool_t pool;

    struct {
        const char *session;

        const struct sockaddr *local_address;
        size_t local_address_length;

        const char *remote_host;
        const char *host;
        const char *accept_language;
        const char *user_agent;
        const char *query_string;
    } request;

    struct translate_response response;
};

struct tcache {
    pool_t pool;

    struct cache *cache;

    struct tstock *stock;
};

struct tcache_request {
    pool_t pool;

    struct tcache *tcache;

    const struct translate_request *request;

    /** are we looking for a "BASE" cache entry? */
    bool find_base;

    const char *key;

    translate_callback_t callback;
    void *ctx;
};

#ifdef CACHE_LOG
#include <daemon/log.h>
#define cache_log(...) daemon_log(__VA_ARGS__)
#else
#define cache_log(...) do {} while (0)
#endif

static const char *
tcache_request_key(const struct translate_request *request)
{
    return request->uri != NULL
        ? request->uri
        : request->widget_type;
}

/* check whether the request could produce a cacheable response */
static bool
tcache_request_evaluate(const struct translate_request *request)
{
    return (request->uri != NULL || request->widget_type != NULL) &&
        request->authorization == NULL &&
        request->param == NULL;
}

/* check whether the response is cacheable */
static bool
tcache_response_evaluate(const struct translate_response *response)
{
    return response != NULL && response->max_age != 0 &&
        response->www_authenticate == NULL &&
        response->authentication_info == NULL &&
        response->status == 0;
}

/**
 * Calculate the suffix relative to a base URI from an incoming URI.
 * Returns NULL if no such suffix is possible (e.g. if the specified
 * URI is not "within" the base, or if there is no base at all).
 *
 * @param uri the URI specified by the tcache client, may be NULL
 * @param base the base URI, as given by the translation server,
 * stored in the cache item, may be NULL
 */
static const char *
base_suffix(const char *uri, const char *base)
{
    size_t uri_length, base_length;

    if (uri == NULL || base == NULL)
        return NULL;

    uri_length = strlen(uri);
    base_length = strlen(base);

    return base_length > 0 && base[base_length - 1] == '/' &&
        uri_length > base_length && memcmp(uri, base, base_length) == 0
        ? uri + base_length
        : NULL;
}

static void
tcache_dup_response(pool_t pool, struct translate_response *dest,
                    const struct translate_response *src)
{
    /* we don't copy the "max_age" attribute, because it's only used
       by the tcache itself */

    dest->status = src->status;

    dest->request_header_forward = src->request_header_forward;
    dest->response_header_forward = src->response_header_forward;

    dest->base = p_strdup_checked(pool, src->base);
    dest->site = p_strdup_checked(pool, src->site);
    dest->document_root = p_strdup_checked(pool, src->document_root);
    dest->redirect = p_strdup_checked(pool, src->redirect);
    dest->bounce = p_strdup_checked(pool, src->bounce);
    dest->scheme = p_strdup_checked(pool, src->scheme);
    dest->host = p_strdup_checked(pool, src->host);
    dest->uri = p_strdup_checked(pool, src->uri);
    dest->untrusted = p_strdup_checked(pool, src->untrusted);
    dest->untrusted_prefix = p_strdup_checked(pool, src->untrusted_prefix);
    dest->stateful = src->stateful;
    dest->discard_session = src->discard_session;
    dest->secure_cookie = src->secure_cookie;
    dest->filter_4xx = src->filter_4xx;
    dest->session = NULL;

    /* The "user" attribute must not be present in cached responses,
       because they belong to only that one session.  For the same
       reason, we won't copy the user_max_age attribute. */
    dest->user = NULL;

    dest->language = NULL;
    dest->www_authenticate = p_strdup_checked(pool, src->www_authenticate);
    dest->authentication_info = p_strdup_checked(pool,
                                                 src->authentication_info);

    dest->headers = src->headers != NULL
        ? strmap_dup(pool, src->headers)
        : NULL;

    dest->views = src->views != NULL
        ? transformation_dup_view_chain(pool, src->views)
        : NULL;

    dest->num_vary = src->num_vary;
    if (dest->num_vary > 0)
        dest->vary = (const uint16_t *)
            p_memdup(pool, src->vary,
                     dest->num_vary * sizeof(dest->vary[0]));

    dest->num_invalidate = src->num_invalidate;
    if (dest->num_invalidate > 0)
        dest->invalidate = (const uint16_t *)
            p_memdup(pool, src->invalidate,
                     dest->num_invalidate * sizeof(dest->invalidate[0]));
}

static size_t
base_string(const char *p, const char *suffix)
{
    size_t length = strlen(p), suffix_length = strlen(suffix);

    return length > suffix_length && p[length - suffix_length - 1] == '/' &&
        memcmp(p + length - suffix_length, suffix, suffix_length) == 0
        ? length - suffix_length
        : 0;
}

/**
 * Copies the address #src to #dest and returns the new cache key.
 * Returns NULL if the cache key should not be modified (i.e. if there
 * is no matching BASE packet).
 */
static const char *
tcache_store_address(pool_t pool, struct resource_address *dest,
                     const struct resource_address *src,
                     const char *uri, const char *base)
{
    const char *suffix = base_suffix(uri, base);
    if (suffix != NULL) {
        /* we received a valid BASE packet - store only the base
           URI */
        struct resource_address *a =
            resource_address_save_base(pool, src, suffix);
        if (a != NULL) {
            *dest = *a;
            return p_strndup(pool, uri, suffix - uri);
        }
    }

    resource_address_copy(pool, dest, src);
    return NULL;
}

static const char *
tcache_store_response(pool_t pool, struct translate_response *dest,
                      const struct translate_response *src,
                      const struct translate_request *request)
{
    const char *key;

    key = tcache_store_address(pool, &dest->address, &src->address,
                               request->uri, src->base);
    tcache_dup_response(pool, dest, src);

    if (key == NULL)
        /* the BASE value didn't match - clear it */
        dest->base = NULL;

    if (dest->uri != NULL) {
        const char *suffix = base_suffix(request->uri, src->base);

        if (suffix != NULL) {
            size_t length = base_string(dest->uri, suffix);
            dest->uri = length > 0
                ? p_strndup(pool, dest->uri, length)
                : NULL;
        }
    }

    return key;
}

/**
 * Load an address from a cached translate_response object, and apply
 * any BASE changes (if a BASE is present).
 */
static void
tcache_load_address(pool_t pool, const char *uri,
                    struct resource_address *dest,
                    const struct translate_response *src)
{
    if (src->base != NULL) {
        struct resource_address *a;

        assert(memcmp(src->base, uri, strlen(src->base)) == 0);

        a = resource_address_load_base(pool, &src->address,
                                       uri + strlen(src->base));
        if (a != NULL) {
            *dest = *a;
            return;
        }
    }

    resource_address_copy(pool, dest, &src->address);
}

static void
tcache_load_response(pool_t pool, struct translate_response *dest,
                     const struct translate_response *src,
                     const char *uri)
{
    tcache_load_address(pool, uri, &dest->address, src);
    tcache_dup_response(pool, dest, src);
}

static bool
tcache_vary_contains(const struct translate_response *response,
                     enum beng_translation_command command)
{
    for (unsigned i = 0; i < response->num_vary; ++i)
        if (response->vary[i] == command)
            return true;

    return false;
}

static const char *
tcache_vary_copy(pool_t pool, const char *p,
                 const struct translate_response *response,
                 enum beng_translation_command command)
{
    return p != NULL && tcache_vary_contains(response, command)
        ? p_strdup(pool, p)
        : NULL;
}

/**
 * @param strict in strict mode, NULL values are a mismatch
 */
static bool
tcache_buffer_match(const void *a, size_t a_length,
                    const void *b, size_t b_length,
                    bool strict)
{
    assert((a == NULL) == (a_length == 0));
    assert((b == NULL) == (b_length == 0));

    if (a == NULL || b == NULL)
        return !strict && a == b;

    if (a_length != b_length)
        return false;

    return memcmp(a, b, a_length) == 0;
}

/**
 * @param strict in strict mode, NULL values are a mismatch
 */
static bool
tcache_string_match(const char *a, const char *b, bool strict)
{
    if (a == NULL || b == NULL)
        return !strict && a == b;

    return strcmp(a, b) == 0;
}

/**
 * @param strict in strict mode, unknown commands and NULL values are
 * a mismatch
 */
static bool
tcache_vary_match(const struct tcache_item *item,
                  const struct translate_request *request,
                  enum beng_translation_command command,
                  bool strict)
{
    switch (command) {
    case TRANSLATE_URI:
        return tcache_string_match(item->item.key,
                                   request->uri, strict);

    case TRANSLATE_SESSION:
        return tcache_string_match(item->request.session,
                                   request->session, strict);

    case TRANSLATE_LOCAL_ADDRESS:
        return tcache_buffer_match(item->request.local_address,
                                   item->request.local_address_length,
                                   request->local_address,
                                   request->local_address_length,
                                   strict);

    case TRANSLATE_REMOTE_HOST:
        return tcache_string_match(item->request.remote_host,
                                   request->remote_host, strict);

    case TRANSLATE_HOST:
        return tcache_string_match(item->request.host, request->host, strict);

    case TRANSLATE_LANGUAGE:
        return tcache_string_match(item->request.accept_language,
                                   request->accept_language, strict);

    case TRANSLATE_USER_AGENT:
        return tcache_string_match(item->request.user_agent,
                                   request->user_agent, strict);

    case TRANSLATE_QUERY_STRING:
        return tcache_string_match(item->request.query_string,
                                   request->query_string, strict);

    default:
        return !strict;
    }
}

static bool
tcache_item_match(const struct cache_item *_item, void *ctx)
{
    const struct tcache_item *item = (const struct tcache_item *)_item;
    struct tcache_request *tcr = ctx;
    const struct translate_request *request = tcr->request;

    if (tcr->find_base && item->response.base == NULL)
        /* this is a "base" lookup, but this response does not contain
           a "BASE" packet */
        return false;

    for (unsigned i = 0; i < item->response.num_vary; ++i)
        if (!tcache_vary_match(item, request,
                               (enum beng_translation_command)item->response.vary[i],
                               false))
            return false;

    return true;
}

static struct tcache_item *
tcache_get(struct tcache *tcache, const struct translate_request *request,
           const char *key, bool find_base)
{
    struct tcache_request match_ctx = {
        .request = request,
        .find_base = find_base,
    };

    return (struct tcache_item *)cache_get_match(tcache->cache, key,
                                                 tcache_item_match, &match_ctx);
}

static struct tcache_item *
tcache_lookup(pool_t pool, struct tcache *tcache,
              const struct translate_request *request, const char *key)
{
    struct tcache_item *item;
    char *uri, *slash;

    item = tcache_get(tcache, request, key, false);
    if (item != NULL || request->uri == NULL)
        return item;

    /* no match - look for matching BASE responses */

    uri = p_strdup(pool, key);
    slash = strrchr(uri, '/');

    if (slash != NULL && slash[1] == 0) {
        /* if the URI already ends with a slash, don't repeat the
           original lookup - cut off this slash, and try again */
        *slash = 0;
        slash = strrchr(uri, '/');
    }

    while (slash != NULL) {
        /* truncate string after slash */
        slash[1] = 0;

        item = tcache_get(tcache, request, uri, true);
        if (item != NULL)
            return item;

        *slash = 0;
        slash = strrchr(uri, '/');
    }

    return NULL;
}

struct tcache_invalidate_data {
    const struct translate_request *request;
    const struct translate_response *response;
};

static bool
tcache_invalidate_match(const struct cache_item *_item, void *ctx)
{
    const struct tcache_item *item = (const struct tcache_item *)_item;
    const struct tcache_invalidate_data *data = ctx;
    const uint16_t *invalidate = data->response->invalidate;
    unsigned num_invalidate = data->response->num_invalidate;

    for (unsigned i = 0; i < num_invalidate; ++i)
        if (!tcache_vary_match(item, data->request,
                               (enum beng_translation_command)invalidate[i],
                               true))
            return false;

    return true;
}


/*
 * translate callback
 *
 */

static void
tcache_callback(const struct translate_response *response, void *ctx)
{
    struct tcache_request *tcr = ctx;

    if (response != NULL && response->num_invalidate > 0) {
        struct tcache_invalidate_data data = {
            .request = tcr->request,
            .response = response,
        };
        unsigned removed;

        removed = cache_remove_all_match(tcr->tcache->cache,
                                         tcache_invalidate_match, &data);
        cache_log(4, "translate_cache: invalidated %u cache items\n", removed);
    }

    if (tcache_response_evaluate(response)) {
        pool_t pool = pool_new_linear(tcr->tcache->pool, "tcache_item", 512);
        struct tcache_item *item = p_malloc(pool, sizeof(*item));
        unsigned max_age = response->max_age;
        const char *key;

        cache_log(4, "translate_cache: store %s\n", tcr->key);

        if (max_age > 300)
            max_age = 300;

        cache_item_init(&item->item, time(NULL) + max_age, 1);
        item->pool = pool;

        item->request.session =
            tcache_vary_copy(pool, tcr->request->session,
                             response, TRANSLATE_SESSION);

        item->request.local_address =
            tcr->request->local_address != NULL &&
            tcache_vary_contains(response, TRANSLATE_LOCAL_ADDRESS)
            ? (const struct sockaddr *)
            p_memdup(pool, tcr->request->local_address,
                     tcr->request->local_address_length)
            : NULL;
        item->request.local_address_length =
            tcr->request->local_address_length;

        tcache_vary_copy(pool, tcr->request->remote_host,
                         response, TRANSLATE_REMOTE_HOST);
        item->request.remote_host =
            tcache_vary_copy(pool, tcr->request->remote_host,
                             response, TRANSLATE_REMOTE_HOST);
        item->request.host = tcache_vary_copy(pool, tcr->request->host,
                                              response, TRANSLATE_HOST);
        item->request.accept_language =
            tcache_vary_copy(pool, tcr->request->accept_language,
                             response, TRANSLATE_LANGUAGE);
        item->request.user_agent =
            tcache_vary_copy(pool, tcr->request->user_agent,
                             response, TRANSLATE_USER_AGENT);
        item->request.query_string =
            tcache_vary_copy(pool, tcr->request->query_string,
                             response, TRANSLATE_QUERY_STRING);

        key = tcache_store_response(pool, &item->response, response,
                                    tcr->request);
        if (key == NULL)
            key = p_strdup(pool, tcr->key);

        cache_put_match(tcr->tcache->cache, key, &item->item,
                        tcache_item_match, tcr);
    } else {
        cache_log(4, "translate_cache: nocache %s\n", tcr->key);
    }

    tcr->callback(response, tcr->ctx);
}

static void
tcache_hit(pool_t pool, const char *key, const struct tcache_item *item,
           translate_callback_t callback, void *ctx)
{
    struct translate_response *response =
        p_malloc(pool, sizeof(*response));

    cache_log(4, "translate_cache: hit %s\n", key);

    tcache_load_response(pool, response, &item->response, key);
    callback(response, ctx);
}

static void
tcache_miss(pool_t pool, struct tcache *tcache,
            const struct translate_request *request, const char *key,
            translate_callback_t callback, void *ctx,
            struct async_operation_ref *async_ref)
{
    struct tcache_request *tcr = p_malloc(pool, sizeof(*tcr));

    cache_log(4, "translate_cache: miss %s\n", key);

    tcr->pool = pool;
    tcr->tcache = tcache;
    tcr->request = request;
    tcr->find_base = false;
    tcr->key = key;
    tcr->callback = callback;
    tcr->ctx = ctx;

    tstock_translate(tcache->stock, pool,
                     request, tcache_callback, tcr, async_ref);
}


/*
 * cache class
 *
 */

static void
tcache_destroy(struct cache_item *_item)
{
    struct tcache_item *item = (struct tcache_item *)_item;

    pool_unref(item->pool);
}

static const struct cache_class tcache_class = {
    .destroy = tcache_destroy,
};


/*
 * constructor
 *
 */

struct tcache *
translate_cache_new(pool_t pool, struct tstock *stock,
                    unsigned max_size)
{
    struct tcache *tcache = p_malloc(pool, sizeof(*tcache));

    assert(stock != NULL);

    pool_ref(pool);

    tcache->pool = pool;
    tcache->cache = cache_new(pool, &tcache_class, 65521, max_size);
    tcache->stock = stock;

    return tcache;
}

void
translate_cache_close(struct tcache *tcache)
{
    assert(tcache != NULL);
    assert(tcache->cache != NULL);
    assert(tcache->stock != NULL);

    cache_close(tcache->cache);

    pool_unref(tcache->pool);
}

void
translate_cache_flush(struct tcache *tcache)
{
    cache_flush(tcache->cache);
}


/*
 * methods
 *
 */

void
translate_cache(pool_t pool, struct tcache *tcache,
                const struct translate_request *request,
                translate_callback_t callback,
                void *ctx,
                struct async_operation_ref *async_ref)
{
    if (tcache_request_evaluate(request)) {
        const char *key = tcache_request_key(request);
        struct tcache_item *item = tcache_lookup(pool, tcache, request, key);

        if (item != NULL)
            tcache_hit(pool, key, item, callback, ctx);
        else
            tcache_miss(pool, tcache, request, key, callback, ctx, async_ref);
    } else {
        cache_log(4, "translate_cache: ignore %s\n",
                  request->uri == NULL ? request->widget_type : request->uri);

        tstock_translate(tcache->stock, pool,
                         request, callback, ctx, async_ref);
    }
}
