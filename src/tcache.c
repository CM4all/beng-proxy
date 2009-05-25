/*
 * Cache for translation server responses.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "tcache.h"
#include "transformation.h"
#include "cache.h"
#include "stock.h"
#include "uri-address.h"
#include "beng-proxy/translation.h"

#include <time.h>
#include <string.h>

struct tcache_item {
    struct cache_item item;

    pool_t pool;

    struct {
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

    struct hstock *tcp_stock;

    const char *socket_path;
};

struct tcache_request {
    pool_t pool;

    struct tcache *tcache;

    const struct translate_request *request;

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


/* check whether the request could produce a cacheable response */
static bool
tcache_request_evaluate(const struct translate_request *request)
{
    return (request->uri != NULL || request->widget_type != NULL) &&
        request->param == NULL;
}

/* check whether the response is cacheable */
static bool
tcache_response_evaluate(const struct translate_response *response)
{
    return response != NULL && response->max_age != 0 &&
        response->status == 0;
}

static inline const char *
p_strdup_checked(pool_t pool, const char *s)
{
    return s == NULL ? NULL : p_strdup(pool, s);
}

static void
tcache_dup_response(pool_t pool, struct translate_response *dest,
                    const struct translate_response *src)
{
    /* we don't copy the "max_age" attribute, because it's only used
       by the tcache itself */

    dest->status = src->status;
    resource_address_copy(pool, &dest->address, &src->address);
    dest->site = p_strdup_checked(pool, src->site);
    dest->document_root = p_strdup_checked(pool, src->document_root);
    dest->redirect = p_strdup_checked(pool, src->redirect);
    dest->host = p_strdup_checked(pool, src->host);
    dest->stateful = src->stateful;
    dest->session = NULL;

    /* The "user" attribute must not be present in cached responses,
       because they belong to only that one session.  For the same
       reason, we won't copy the user_max_age attribute. */
    dest->user = NULL;

    dest->language = NULL;

    dest->views = src->views != NULL
        ? transformation_dup_view_chain(pool, src->views)
        : NULL;

    dest->num_vary = src->num_vary;
    if (dest->num_vary > 0)
        dest->vary = (const uint16_t *)
            p_memdup(pool, src->vary,
                     dest->num_vary * sizeof(dest->vary[0]));
}

static void
tcache_store_response(pool_t pool, struct translate_response *dest,
                      const struct translate_response *src)
{
    tcache_dup_response(pool, dest, src);
}

static void
tcache_load_response(pool_t pool, struct translate_response *dest,
                     const struct translate_response *src)
{
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

static bool
tcache_string_match(const char *a, const char *b)
{
    if (a == NULL || b == NULL)
        return a == b;

    return strcmp(a, b);
}

static bool
tcache_vary_match(const struct tcache_item *item,
                  const struct translate_request *request,
                  enum beng_translation_command command)
{
    switch (command) {
    case TRANSLATE_REMOTE_HOST:
        return tcache_string_match(item->request.remote_host,
                                   request->remote_host);

    case TRANSLATE_HOST:
        return tcache_string_match(item->request.host, request->host);

    case TRANSLATE_LANGUAGE:
        return tcache_string_match(item->request.accept_language,
                                   request->accept_language);

    case TRANSLATE_USER_AGENT:
        return tcache_string_match(item->request.user_agent,
                                   request->user_agent);

    case TRANSLATE_QUERY_STRING:
        return tcache_string_match(item->request.query_string,
                                   request->query_string);

    default:
        return true;
    }
}

static bool
tcache_item_match(const struct cache_item *_item, void *ctx)
{
    const struct tcache_item *item = (const struct tcache_item *)_item;
    struct tcache_request *tcr = ctx;
    const struct translate_request *request = tcr->request;

    for (unsigned i = 0; i < item->response.num_vary; ++i)
        if (!tcache_vary_match(item, request,
                               (enum beng_translation_command)item->response.vary[i]))
            return false;

    return true;
}

static struct tcache_item *
tcache_get(struct tcache *tcache, const struct translate_request *request,
           const char *key)
{
    struct tcache_request match_ctx = {
        .request = request,
    };

    return (struct tcache_item *)cache_get_match(tcache->cache, key,
                                                 tcache_item_match, &match_ctx);
}

static struct tcache_item *
tcache_lookup(struct tcache *tcache,
              const struct translate_request *request, const char *key)
{
    return tcache_get(tcache, request, key);
}


/*
 * translate callback
 *
 */

static void
tcache_callback(const struct translate_response *response, void *ctx)
{
    struct tcache_request *tcr = ctx;

    if (tcache_response_evaluate(response)) {
        pool_t pool = pool_new_linear(tcr->tcache->pool, "tcache_item", 512);
        struct tcache_item *item = p_malloc(pool, sizeof(*item));
        unsigned max_age = response->max_age;

        cache_log(4, "translate_cache: store %s\n", tcr->key);

        if (max_age > 300)
            max_age = 300;

        cache_item_init(&item->item, time(NULL) + max_age, 1);
        item->pool = pool;

        item->request.remote_host =
            tcache_vary_copy(pool, tcr->request->remote_host,
                             response, TRANSLATE_REMOTE_HOST);
        item->request.host = tcache_vary_copy(pool, tcr->request->remote_host,
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

        tcache_store_response(pool, &item->response, response);
        cache_put_match(tcr->tcache->cache, p_strdup(pool, tcr->key), &item->item,
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

    tcache_load_response(pool, response, &item->response);
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
    tcr->key = key;
    tcr->callback = callback;
    tcr->ctx = ctx;

    translate(pool, tcache->tcp_stock, tcache->socket_path,
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
translate_cache_new(pool_t pool, struct hstock *tcp_stock,
                    const char *socket_path)
{
    struct tcache *tcache = p_malloc(pool, sizeof(*tcache));

    assert(tcp_stock != NULL);
    assert(socket_path != NULL);

    pool_ref(pool);

    tcache->pool = pool;
    tcache->cache = cache_new(pool, &tcache_class, 1024);
    tcache->tcp_stock = tcp_stock;
    tcache->socket_path = socket_path;

    return tcache;
}

void
translate_cache_close(struct tcache *tcache)
{
    assert(tcache != NULL);
    assert(tcache->cache != NULL);
    assert(tcache->tcp_stock != NULL);
    assert(tcache->socket_path != NULL);

    cache_close(tcache->cache);

    pool_unref(tcache->pool);
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
        const char *key = request->uri == NULL
            ? request->widget_type : request->uri;
        struct tcache_item *item = tcache_lookup(tcache, request, key);

        if (item != NULL)
            tcache_hit(pool, key, item, callback, ctx);
        else
            tcache_miss(pool, tcache, request, key, callback, ctx, async_ref);
    } else {
        cache_log(4, "translate_cache: ignore %s\n",
                  request->uri == NULL ? request->widget_type : request->uri);

        translate(pool, tcache->tcp_stock, tcache->socket_path,
                  request, callback, ctx, async_ref);
    }
}
