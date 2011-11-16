/*
 * Cache for translation server responses.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "tcache.h"
#include "tstock.h"
#include "translate.h"
#include "widget-class.h"
#include "cache.h"
#include "stock.h"
#include "strmap.h"
#include "uri-address.h"
#include "strref-pool.h"
#include "beng-proxy/translation.h"

#include <time.h>
#include <string.h>
#include <stdlib.h>

struct tcache_item {
    struct cache_item item;

    struct pool *pool;

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

    GRegex *regex, *inverse_regex;
};

struct tcache {
    struct pool *pool;

    struct cache *cache;

    struct tstock *stock;
};

struct tcache_request {
    struct pool *pool;

    struct tcache *tcache;

    const struct translate_request *request;

    /** are we looking for a "BASE" cache entry? */
    bool find_base;

    const char *key;

    const struct translate_handler *handler;
    void *handler_ctx;
};

#ifdef CACHE_LOG
#include <daemon/log.h>
#define cache_log(...) daemon_log(__VA_ARGS__)
#else
#define cache_log(...) do {} while (0)
#endif

static const char *
tcache_uri_key(struct pool *pool, const char *uri, http_status_t status,
               const struct strref *check)
{
    const char *key = status != 0
        ? p_sprintf(pool, "ERR%u_%s", status, uri)
        : uri;

    if (check != NULL && !strref_is_null(check))
        key = p_strncat(pool, key, strlen(key),
                        "|CHECK=", (size_t)7,
                        check->data, (size_t)check->length,
                        NULL);

    return key;

}

static const char *
tcache_request_key(struct pool *pool, const struct translate_request *request)
{
    return request->uri != NULL
        ? tcache_uri_key(pool, request->uri, request->error_document_status,
                         &request->check)
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
    assert(response != NULL);

    return response->max_age != 0 &&
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
tcache_dup_response(struct pool *pool, struct translate_response *dest,
                    const struct translate_response *src)
{
    /* we don't copy the "max_age" attribute, because it's only used
       by the tcache itself */

    dest->status = src->status;

    dest->request_header_forward = src->request_header_forward;
    dest->response_header_forward = src->response_header_forward;

    dest->base = p_strdup_checked(pool, src->base);
    dest->regex = p_strdup_checked(pool, src->regex);
    dest->inverse_regex = p_strdup_checked(pool, src->inverse_regex);
    dest->site = p_strdup_checked(pool, src->site);
    dest->document_root = p_strdup_checked(pool, src->document_root);
    dest->redirect = p_strdup_checked(pool, src->redirect);
    dest->bounce = p_strdup_checked(pool, src->bounce);
    dest->scheme = p_strdup_checked(pool, src->scheme);
    dest->host = p_strdup_checked(pool, src->host);
    dest->uri = p_strdup_checked(pool, src->uri);
    dest->untrusted = p_strdup_checked(pool, src->untrusted);
    dest->untrusted_prefix = p_strdup_checked(pool, src->untrusted_prefix);
    dest->untrusted_site_suffix =
        p_strdup_checked(pool, src->untrusted_site_suffix);
    dest->stateful = src->stateful;
    dest->discard_session = src->discard_session;
    dest->secure_cookie = src->secure_cookie;
    dest->filter_4xx = src->filter_4xx;
    dest->error_document = src->error_document;
    dest->previous = src->previous;
    dest->transparent = src->transparent;
    dest->anchor_absolute = src->anchor_absolute;
    dest->dump_headers = src->dump_headers;
    dest->session = NULL;

    if (strref_is_null(&src->check))
        strref_null(&dest->check);
    else
        strref_set_dup(pool, &dest->check, &src->check);

    /* The "user" attribute must not be present in cached responses,
       because they belong to only that one session.  For the same
       reason, we won't copy the user_max_age attribute. */
    dest->user = NULL;

    dest->language = NULL;
    dest->realm = p_strdup_checked(pool, src->realm);
    dest->www_authenticate = p_strdup_checked(pool, src->www_authenticate);
    dest->authentication_info = p_strdup_checked(pool,
                                                 src->authentication_info);
    dest->cookie_host = p_strdup_checked(pool, src->cookie_host);

    dest->headers = src->headers != NULL
        ? strmap_dup(pool, src->headers)
        : NULL;

    dest->views = src->views != NULL
        ? widget_view_dup_chain(pool, src->views)
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
tcache_store_address(struct pool *pool, struct resource_address *dest,
                     const struct resource_address *src,
                     const char *uri, const char *base)
{
    const char *suffix = base_suffix(uri, base);
    if (suffix != NULL) {
        /* we received a valid BASE packet - store only the base
           URI */
        if (resource_address_save_base(pool, dest, src, suffix) != NULL)
            return p_strndup(pool, uri, suffix - uri);
    }

    resource_address_copy(pool, dest, src);
    return NULL;
}

static const char *
tcache_store_response(struct pool *pool, struct translate_response *dest,
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

    if (key != NULL)
        key = tcache_uri_key(pool, key, request->error_document_status,
                             &request->check);

    return key;
}

/**
 * Load an address from a cached translate_response object, and apply
 * any BASE changes (if a BASE is present).
 */
static void
tcache_load_address(struct pool *pool, const char *uri,
                    struct resource_address *dest,
                    const struct translate_response *src)
{
    if (src->base != NULL) {
        assert(memcmp(src->base, uri, strlen(src->base)) == 0);

        if (resource_address_load_base(pool, dest, &src->address,
                                       uri + strlen(src->base)) != NULL)
            return;
    }

    resource_address_copy(pool, dest, &src->address);
}

static void
tcache_load_response(struct pool *pool, struct translate_response *dest,
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
tcache_vary_copy(struct pool *pool, const char *p,
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
 * @param strict in strict mode, NULL values are a mismatch
 */
static bool
tcache_uri_match(const char *a, const char *b, bool strict)
{
    if (a == NULL || b == NULL)
        return !strict && a == b;

    if (memcmp(a, "ERR", 3) == 0) {
        char *endptr;
        strtol(a + 3, &endptr, 10);
        if (*endptr == '_')
            a = endptr + 1;
    }

    const char *check = strstr(a, "|CHECK=");
    if (check != NULL)
        return memcmp(a, b, check - a) == 0 && b[check - a] == 0;
    else
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
        return tcache_uri_match(item->item.key,
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

    if (item->response.base != NULL && item->inverse_regex != NULL &&
        request->uri != NULL &&
        g_regex_match(item->inverse_regex, request->uri, 0, NULL))
        /* the URI matches the inverse regular expression */
        return false;

    if (item->response.base != NULL && item->regex != NULL &&
        (request->uri == NULL ||
         !g_regex_match(item->regex, request->uri, 0, NULL)))
        /* the URI did not match the regular expression */
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
tcache_lookup(struct pool *pool, struct tcache *tcache,
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

    const uint16_t *vary;
    unsigned num_vary;

    const char *site;
 };

static bool
tcache_invalidate_match(const struct cache_item *_item, void *ctx)
{
    const struct tcache_item *item = (const struct tcache_item *)_item;
    const struct tcache_invalidate_data *data = ctx;
    const uint16_t *invalidate = data->vary;
    unsigned num_invalidate = data->num_vary;

    if (data->site != NULL &&
        (item->response.site == NULL || strcmp(data->site, item->response.site) != 0))
        return false;

    for (unsigned i = 0; i < num_invalidate; ++i)
        if (!tcache_vary_match(item, data->request,
                               (enum beng_translation_command)invalidate[i],
                               true))
            return false;

    return true;
}

void
translate_cache_invalidate(struct tcache *tcache,
                           const struct translate_request *request,
                           const uint16_t *vary, unsigned num_vary,
                           const char *site)
{
    struct tcache_invalidate_data data = {
        .request = request,
        .vary = vary,
        .num_vary = num_vary,
        .site = site,
    };

    unsigned removed = cache_remove_all_match(tcache->cache,
                                              tcache_invalidate_match, &data);
    cache_log(4, "translate_cache: invalidated %u cache items\n", removed);
}


/*
 * translate callback
 *
 */

static void
tcache_handler_response(const struct translate_response *response, void *ctx)
{
    struct tcache_request *tcr = ctx;

    assert(response != NULL);

    if (response->num_invalidate > 0)
        translate_cache_invalidate(tcr->tcache, tcr->request,
                                   response->invalidate,
                                   response->num_invalidate,
                                   NULL);

    if (tcache_response_evaluate(response)) {
        struct pool *pool = pool_new_linear(tcr->tcache->pool, "tcache_item", 512);
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

        if (response->regex != NULL) {
            GError *error = NULL;
            item->regex = g_regex_new(response->regex,
                                      G_REGEX_MULTILINE|G_REGEX_DOTALL|
                                      G_REGEX_RAW|G_REGEX_NO_AUTO_CAPTURE|
                                      G_REGEX_OPTIMIZE,
                                      0, &error);
            if (item->regex == NULL) {
                cache_log(2, "translate_cache: failed to compile regular expression: %s",
                          error->message);
                g_error_free(error);
            }
        } else
            item->regex = NULL;

        if (response->inverse_regex != NULL) {
            GError *error = NULL;
            item->inverse_regex = g_regex_new(response->inverse_regex,
                                              G_REGEX_MULTILINE|G_REGEX_DOTALL|
                                              G_REGEX_RAW|G_REGEX_NO_AUTO_CAPTURE|
                                              G_REGEX_OPTIMIZE,
                                              0, &error);
            if (item->inverse_regex == NULL) {
                cache_log(2, "translate_cache: failed to compile regular expression: %s",
                          error->message);
                g_error_free(error);
            }
        } else
            item->inverse_regex = NULL;

        cache_put_match(tcr->tcache->cache, key, &item->item,
                        tcache_item_match, tcr);
    } else {
        cache_log(4, "translate_cache: nocache %s\n", tcr->key);
    }

    tcr->handler->response(response, tcr->handler_ctx);
}

static void
tcache_handler_error(GError *error, void *ctx)
{
    struct tcache_request *tcr = ctx;

    cache_log(4, "translate_cache: error %s\n", tcr->key);

    tcr->handler->error(error, tcr->handler_ctx);
}

static const struct translate_handler tcache_handler = {
    .response = tcache_handler_response,
    .error = tcache_handler_error,
};

static void
tcache_hit(struct pool *pool, const char *key, const struct tcache_item *item,
           const struct translate_handler *handler, void *ctx)
{
    struct translate_response *response =
        p_malloc(pool, sizeof(*response));

    cache_log(4, "translate_cache: hit %s\n", key);

    tcache_load_response(pool, response, &item->response, key);
    handler->response(response, ctx);
}

static void
tcache_miss(struct pool *pool, struct tcache *tcache,
            const struct translate_request *request, const char *key,
            const struct translate_handler *handler, void *ctx,
            struct async_operation_ref *async_ref)
{
    struct tcache_request *tcr = p_malloc(pool, sizeof(*tcr));

    cache_log(4, "translate_cache: miss %s\n", key);

    tcr->pool = pool;
    tcr->tcache = tcache;
    tcr->request = request;
    tcr->find_base = false;
    tcr->key = key;
    tcr->handler = handler;
    tcr->handler_ctx = ctx;

    tstock_translate(tcache->stock, pool,
                     request, &tcache_handler, tcr, async_ref);
}


/*
 * cache class
 *
 */

static void
tcache_destroy(struct cache_item *_item)
{
    struct tcache_item *item = (struct tcache_item *)_item;

    if (item->regex != NULL)
        g_regex_unref(item->regex);

    if (item->inverse_regex != NULL)
        g_regex_unref(item->inverse_regex);

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
translate_cache_new(struct pool *pool, struct tstock *stock,
                    unsigned max_size)
{
    pool = pool_new_libc(pool, "translate_cache");
    struct tcache *tcache = p_malloc(pool, sizeof(*tcache));

    assert(stock != NULL);

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
translate_cache(struct pool *pool, struct tcache *tcache,
                const struct translate_request *request,
                const struct translate_handler *handler, void *ctx,
                struct async_operation_ref *async_ref)
{
    if (tcache_request_evaluate(request)) {
        const char *key = tcache_request_key(pool, request);
        struct tcache_item *item = tcache_lookup(pool, tcache, request, key);

        if (item != NULL)
            tcache_hit(pool, key, item, handler, ctx);
        else
            tcache_miss(pool, tcache, request, key, handler, ctx, async_ref);
    } else {
        cache_log(4, "translate_cache: ignore %s\n",
                  request->uri == NULL ? request->widget_type : request->uri);

        tstock_translate(tcache->stock, pool,
                         request, handler, ctx, async_ref);
    }
}
