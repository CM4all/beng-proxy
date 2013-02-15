/*
 * Cache for translation server responses.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "tcache.h"
#include "tstock.h"
#include "translate-request.h"
#include "translate-response.h"
#include "translate-client.h"
#include "http-quark.h"
#include "widget-class.h"
#include "cache.h"
#include "stock.h"
#include "hashmap.h"
#include "uri-address.h"
#include "uri-verify.h"
#include "strref-pool.h"
#include "slice.h"
#include "beng-proxy/translation.h"

#include <inline/list.h>

#include <time.h>
#include <string.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <errno.h>

struct tcache_item {
    struct cache_item item;

    /**
     * A double linked list of cache items with the same HOST request
     * string.  Only those that had VARY=HOST in the response are
     * added to the list.  Check list_empty() on this attribute to
     * check whether this item lives in such a list.
     */
    struct list_head per_host_siblings;

    struct pool *pool;

    struct {
        const char *session;

        const struct sockaddr *local_address;
        size_t local_address_length;

        const char *remote_host;
        const char *host;
        const char *accept_language;
        const char *user_agent;
        const char *ua_class;
        const char *query_string;
    } request;

    struct translate_response response;

    GRegex *regex, *inverse_regex;
};

struct tcache_per_host {
    /**
     * A double-linked list of #tcache_items (by its attribute
     * per_host_siblings).
     *
     * This must be the first attribute in the struct.
     */
    struct list_head items;

    struct tcache *tcache;

    /**
     * A pointer to the hashmap key, for use with p_free().
     */
    char *host;
};

struct tcache {
    struct pool *pool;
    struct slice_pool *slice_pool;

    struct cache *cache;

    /**
     * This hash table maps each host name to a #tcache_per.  This is
     * used to optimize the common INVALIDATE=HOST response, to avoid
     * traversing the whole cache.
     */
    struct hashmap *per_host;

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

static const GRegexCompileFlags default_regex_compile_flags =
    G_REGEX_MULTILINE|G_REGEX_DOTALL|
    G_REGEX_RAW|G_REGEX_NO_AUTO_CAPTURE|
    G_REGEX_OPTIMIZE;

#ifdef CACHE_LOG
#include <daemon/log.h>
#define cache_log(...) daemon_log(__VA_ARGS__)
#else
#define cache_log(...) do {} while (0)
#endif

static struct tcache_item *
cast_per_host_sibling_to_item(struct list_head *lh)
{
    return (struct tcache_item *)(((char *)lh) - offsetof(struct tcache_item,
                                                          per_host_siblings));
}

static void
tcache_add_per_host(struct tcache *tcache, struct tcache_item *item)
{
    assert(translate_response_vary_contains(&item->response, TRANSLATE_HOST));

    const char *host = item->request.host;
    if (host == NULL)
        host = "";

    struct tcache_per_host *per_host = hashmap_get(tcache->per_host, host);
    if (per_host == NULL) {
        per_host = p_malloc(tcache->pool, sizeof(*per_host));
        list_init(&per_host->items);
        per_host->tcache = tcache;
        per_host->host = p_strdup(tcache->pool, host);

        hashmap_add(tcache->per_host, per_host->host, per_host);
    }

    list_add(&item->per_host_siblings, &per_host->items);
}

static void
tcache_remove_per_host(struct tcache_item *item)
{
    assert(!list_empty(&item->per_host_siblings));
    assert(translate_response_vary_contains(&item->response, TRANSLATE_HOST));

    struct list_head *next = item->per_host_siblings.next;
    list_remove(&item->per_host_siblings);

    if (list_empty(next)) {
        /* if the next item is now empty, this can only be the
           per_host object - delete it */
        struct tcache_per_host *per_host = (struct tcache_per_host *)next;

        const char *host = item->request.host;
        if (host == NULL)
            host = "";

        assert(strcmp(per_host->host, host) == 0);

        struct tcache *tcache = per_host->tcache;

        gcc_unused
        const void *old = hashmap_remove(tcache->per_host, host);
        assert(old == per_host);

        p_free(tcache->pool, per_host->host);
        p_free(tcache->pool, per_host);
    }
}

static const char *
tcache_uri_key(struct pool *pool, const char *uri, const char *host,
               http_status_t status,
               const struct strref *check)
{
    const char *key = status != 0
        ? p_sprintf(pool, "ERR%u_%s", status, uri)
        : uri;

    if (host != NULL)
        /* workaround for a scalability problem in a large hosting
           environment: include the Host request header in the cache
           key */
        key = p_strcat(pool, host, ":", key, NULL);

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
        ? tcache_uri_key(pool, request->uri, request->host,
                         request->error_document_status,
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
 * Expand EXPAND_PATH_INFO specifications in all #resource_address
 * instances.
 */
static bool
tcache_expand_response(struct pool *pool, struct translate_response *response,
                       const struct tcache_item *item, const char *uri,
                       GError **error_r)
{
    assert(pool != NULL);
    assert(response != NULL);
    assert(item != NULL);

    if (uri == NULL || item->regex == NULL)
        return true;

    assert(response->regex != NULL);
    assert(response->base != NULL);

    GMatchInfo *match_info;
    if (!g_regex_match(item->regex, uri, 0, &match_info)) {
        /* shouldn't happen, as this has already been matched */
        g_set_error(error_r, http_response_quark(),
                    HTTP_STATUS_BAD_REQUEST, "Regex mismatch");
        return false;
    }

    bool success = translate_response_expand(pool, response,
                                             match_info, error_r);
    g_match_info_free(match_info);
    return success;
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
                     const char *uri, const char *base, bool expandable)
{
    const char *suffix = base_suffix(uri, base);
    if (suffix != NULL) {
        /* we received a valid BASE packet - store only the base
           URI */

        if (expandable) {
            /* when the response is expandable, skip appending the
               base suffix, don't call resource_address_save_base() */
            resource_address_copy(pool, dest, src);
            return p_strndup(pool, uri, suffix - uri);
        }

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
    const char *base = src->base;
    char *new_base = NULL;

    if (src->auto_base && base == NULL && request->uri != NULL)
        base = new_base = resource_address_auto_base(pool, &src->address,
                                                     request->uri);

    const char *key;

    key = tcache_store_address(pool, &dest->address, &src->address,
                               request->uri, base,
                               translate_response_is_expandable(src));
    translate_response_copy(pool, dest, src);

    if (key == NULL)
        /* the BASE value didn't match - clear it */
        dest->base = NULL;
    else if (new_base != NULL)
        dest->base = new_base;

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
        key = tcache_uri_key(pool, key, request->host,
                             request->error_document_status,
                             &request->check);

    return key;
}

/**
 * Load an address from a cached translate_response object, and apply
 * any BASE changes (if a BASE is present).
 */
static bool
tcache_load_address(struct pool *pool, const char *uri,
                    struct resource_address *dest,
                    const struct translate_response *src,
                    GError **error_r)
{
    if (src->base != NULL && !translate_response_is_expandable(src)) {
        assert(memcmp(src->base, uri, strlen(src->base)) == 0);

        const char *suffix = uri + strlen(src->base);

        if (!uri_path_verify_paranoid(suffix - 1)) {
            g_set_error(error_r, http_response_quark(),
                        HTTP_STATUS_BAD_REQUEST, "Malformed URI");
            return false;
        }

        if (resource_address_load_base(pool, dest, &src->address,
                                       suffix) != NULL)
            return true;
    }

    resource_address_copy(pool, dest, &src->address);
    return true;
}

static bool
tcache_load_response(struct pool *pool, struct translate_response *dest,
                     const struct translate_response *src,
                     const char *uri, GError **error_r)
{
    if (!tcache_load_address(pool, uri, &dest->address, src, error_r))
        return false;

    translate_response_copy(pool, dest, src);
    return true;
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

    case TRANSLATE_UA_CLASS:
        return tcache_string_match(item->request.ua_class,
                                   request->ua_class, strict);

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

static unsigned
translate_cache_invalidate_host(struct tcache *tcache, const char *host)
{
    if (host == NULL)
        host = "";

    struct tcache_per_host *per_host = hashmap_get(tcache->per_host, host);
    if (per_host == NULL)
        return 0;

    assert(per_host->tcache == tcache);
    assert(strcmp(per_host->host, host) == 0);

    unsigned n_removed = 0;
    bool done;
    do {
        assert(!list_empty(&per_host->items));

        struct tcache_item *item =
            cast_per_host_sibling_to_item(per_host->items.next);

        /* we're done when we're about to remove the last item - the
           last item will destroy the #tcache_per_host object, so we
           need to check the condition before removing the cache
           item */
        done = item->per_host_siblings.next == &per_host->items;

        cache_remove_item(tcache->cache, item->item.key, &item->item);
        ++n_removed;
    } while (!done);

    return n_removed;
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

    /* TODO: optimize the case when there is more than just
       TRANSLATE_HOST */
    gcc_unused
    unsigned removed = num_vary == 1 && vary[0] == TRANSLATE_HOST
        ? translate_cache_invalidate_host(tcache, request->host)
        : cache_remove_all_match(tcache->cache,
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
        struct pool *pool = pool_new_slice(tcr->tcache->pool, "tcache_item",
                                           tcr->tcache->slice_pool);
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
        item->request.ua_class =
            tcache_vary_copy(pool, tcr->request->ua_class,
                             response, TRANSLATE_UA_CLASS);
        item->request.query_string =
            tcache_vary_copy(pool, tcr->request->query_string,
                             response, TRANSLATE_QUERY_STRING);

        key = tcache_store_response(pool, &item->response, response,
                                    tcr->request);
        if (key == NULL)
            key = p_strdup(pool, tcr->key);

        if (response->regex != NULL) {
            GRegexCompileFlags compile_flags = default_regex_compile_flags;
            if (translate_response_is_expandable(response))
                /* enable capturing if we need the match groups */
                compile_flags &= ~G_REGEX_NO_AUTO_CAPTURE;

            GError *error = NULL;
            item->regex = g_regex_new(response->regex,
                                      compile_flags,
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
                                              default_regex_compile_flags,
                                              0, &error);
            if (item->inverse_regex == NULL) {
                cache_log(2, "translate_cache: failed to compile regular expression: %s",
                          error->message);
                g_error_free(error);
            }
        } else
            item->inverse_regex = NULL;

        if (translate_response_vary_contains(response, TRANSLATE_HOST))
            tcache_add_per_host(tcr->tcache, item);
        else
            list_init(&item->per_host_siblings);

        cache_put_match(tcr->tcache->cache, key, &item->item,
                        tcache_item_match, tcr);

        if (tcr->request->uri != NULL &&
            translate_response_is_expandable(response)) {
            /* create a writable copy and expand it */
            struct translate_response *response2 =
                p_memdup(pool, response, sizeof(*response));

            GError *error = NULL;
            if (!tcache_expand_response(pool, response2, item,
                                        tcr->request->uri, &error)) {
                tcr->handler->error(error, tcr->handler_ctx);
                return;
            }

            response = response2;
        }
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
tcache_hit(struct pool *pool, const char *uri, const char *key,
           const struct tcache_item *item,
           const struct translate_handler *handler, void *ctx)
{
    struct translate_response *response =
        p_malloc(pool, sizeof(*response));

    cache_log(4, "translate_cache: hit %s\n", key);

    GError *error = NULL;
    if (!tcache_load_response(pool, response, &item->response, key, &error)) {
        handler->error(error, ctx);
        return;
    }

    if (uri != NULL && translate_response_is_expandable(response) &&
        !tcache_expand_response(pool, response, item, uri, &error)) {
        handler->error(error, ctx);
        return;
    }

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

gcc_pure
static bool
tcache_validate_mtime(const struct translate_response *response,
                      gcc_unused const char *key)
{
    if (response->validate_mtime.path == NULL)
        return true;

    cache_log(6, "translate_cache: [%s] validate_mtime %llu %s\n",
              key, (unsigned long long)response->validate_mtime.mtime,
              response->validate_mtime.path);

    struct stat st;
    if (lstat(response->validate_mtime.path, &st) < 0) {
        cache_log(3, "translate_cache: [%s] failed to stat '%s': %s\n",
                  key, response->validate_mtime.path, g_strerror(errno));
        return false;
    }

    if (!S_ISREG(st.st_mode)) {
        cache_log(3, "translate_cache: [%s] not a regular file: %s\n",
                  key, response->validate_mtime.path);
        return false;
    }

    if (st.st_mtime == (time_t)response->validate_mtime.mtime) {
        cache_log(6, "translate_cache: [%s] validate_mtime unmodified %s\n",
                  key, response->validate_mtime.path);
        return true;
    } else {
        cache_log(5, "translate_cache: [%s] validate_mtime modified %s\n",
                  key, response->validate_mtime.path);
        return false;
    }
}


/*
 * cache class
 *
 */

static bool
tcache_validate(struct cache_item *_item)
{
    struct tcache_item *item = (struct tcache_item *)_item;

    return tcache_validate_mtime(&item->response, item->item.key);
}

static void
tcache_destroy(struct cache_item *_item)
{
    struct tcache_item *item = (struct tcache_item *)_item;

    if (!list_empty(&item->per_host_siblings))
        tcache_remove_per_host(item);

    if (item->regex != NULL)
        g_regex_unref(item->regex);

    if (item->inverse_regex != NULL)
        g_regex_unref(item->inverse_regex);

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
translate_cache_new(struct pool *pool, struct tstock *stock,
                    unsigned max_size)
{
    pool = pool_new_libc(pool, "translate_cache");
    struct tcache *tcache = p_malloc(pool, sizeof(*tcache));

    assert(stock != NULL);

    tcache->pool = pool;
    tcache->slice_pool = slice_pool_new(1024, 65536);
    tcache->cache = cache_new(pool, &tcache_class, 65521, max_size);
    tcache->per_host = hashmap_new(pool, 3779);
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
    slice_pool_free(tcache->slice_pool);

    pool_unref(tcache->pool);
}

void
translate_cache_get_stats(const struct tcache *tcache,
                          struct cache_stats *data)
{
    cache_get_stats(tcache->cache, data);
}

void
translate_cache_flush(struct tcache *tcache)
{
    cache_flush(tcache->cache);
    slice_pool_compress(tcache->slice_pool);
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
            tcache_hit(pool, request->uri, key, item, handler, ctx);
        else
            tcache_miss(pool, tcache, request, key, handler, ctx, async_ref);
    } else {
        cache_log(4, "translate_cache: ignore %s\n",
                  request->uri == NULL ? request->widget_type : request->uri);

        tstock_translate(tcache->stock, pool,
                         request, handler, ctx, async_ref);
    }
}
