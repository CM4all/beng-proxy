/*
 * Cache for translation server responses.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "tcache.h"
#include "cache.h"
#include "stock.h"
#include "uri-address.h"

#include <time.h>

struct tcache_item {
    struct cache_item item;

    pool_t pool;

    struct translate_response response;
};

struct tcache {
    pool_t pool;

    struct cache *cache;

    struct stock *stock;
};

struct tcache_request {
    pool_t pool;

    struct tcache *tcache;

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
    return response != NULL && response->status == 0;
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
    const struct translate_transformation *transformation;
    struct translate_transformation **tail_p;

    dest->status = src->status;
    resource_address_copy(pool, &dest->address, &src->address);
    dest->site = p_strdup_checked(pool, src->site);
    dest->document_root = p_strdup_checked(pool, src->document_root);
    dest->redirect = p_strdup_checked(pool, src->redirect);
    dest->google_gadget = src->google_gadget;
    dest->session = NULL;
    dest->user = NULL;
    dest->language = NULL;

    dest->transformation = NULL;
    tail_p = &dest->transformation;
    for (transformation = src->transformation;
         transformation != NULL; transformation = transformation->next) {
        struct translate_transformation *p = p_malloc(pool, sizeof(*p));

        p->type = transformation->type;
        switch (p->type) {
        case TRANSFORMATION_PROCESS:
            p->u.processor_options = transformation->u.processor_options;
            break;

        case TRANSFORMATION_FILTER:
            resource_address_copy(pool, &p->u.filter,
                                  &transformation->u.filter);
            break;
        }

        p->next = NULL;
        *tail_p = p;
        tail_p = &p->next;
    }
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
        pool_t pool = pool_new_linear(tcr->tcache->pool, "tcache_item", 256);
        struct tcache_item *item = p_malloc(pool, sizeof(*item));

        cache_log(4, "translate_cache: store %s\n", tcr->key);

        item->item.expires = time(NULL) + 300;
        item->item.size = 1;
        item->pool = pool;

        tcache_dup_response(pool, &item->response, response);
        cache_put(tcr->tcache->cache, p_strdup(pool, tcr->key), &item->item);
    } else {
        cache_log(4, "translate_cache: nocache %s\n", tcr->key);
    }

    tcr->callback(response, tcr->ctx);
}


/*
 * cache class
 *
 */

static int
tcache_validate(struct cache_item *item __attr_unused)
{
    return 1;
}

static void
tcache_destroy(struct cache_item *_item)
{
    struct tcache_item *item = (struct tcache_item *)_item;

    pool_unref(item->pool);
}

static const struct cache_class tcache_class = {
    .validate = tcache_validate,
    .destroy = tcache_destroy,
};


/*
 * constructor
 *
 */

struct tcache *
translate_cache_new(pool_t pool, struct stock *translate_stock)
{
    struct tcache *tcache = p_malloc(pool, sizeof(*tcache));

    pool_ref(pool);

    tcache->pool = pool;
    tcache->cache = cache_new(pool, &tcache_class, 1024);
    tcache->stock = translate_stock;

    return tcache;
}

void
translate_cache_close(struct tcache *tcache)
{
    assert(tcache != NULL);
    assert(tcache->cache != NULL);
    assert(tcache->stock != NULL);

    cache_close(tcache->cache);
    stock_free(&tcache->stock);

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
        struct tcache_item *item =
            (struct tcache_item *)cache_get(tcache->cache, key);

        if (item == NULL) {
            struct tcache_request *tcr = p_malloc(pool, sizeof(*tcr));

            cache_log(4, "translate_cache: miss %s\n", key);

            tcr->pool = pool;
            tcr->tcache = tcache;
            tcr->key = key;
            tcr->callback = callback;
            tcr->ctx = ctx;

            translate(pool, tcache->stock, request,
                      tcache_callback, tcr, async_ref);
        } else {
            struct translate_response *response =
                p_malloc(pool, sizeof(*response));

            cache_log(4, "translate_cache: hit %s\n", key);

            tcache_dup_response(pool, response, &item->response);
            callback(response, ctx);
        }
    } else {
        cache_log(4, "translate_cache: ignore %s\n",
                  request->uri == NULL ? request->widget_type : request->uri);

        translate(pool, tcache->stock, request, callback, ctx, async_ref);
    }
}
