/*
 * Cache for translation server responses.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "tcache.hxx"
#include "tstock.hxx"
#include "translate_request.hxx"
#include "translate_response.hxx"
#include "translate_client.hxx"
#include "http_quark.h"
#include "widget-class.h"
#include "cache.h"
#include "stock.h"
#include "hashmap.h"
#include "http_address.h"
#include "uri_base.hxx"
#include "uri-verify.h"
#include "uri-escape.h"
#include "uri_base.hxx"
#include "strref-pool.h"
#include "slice.h"
#include "beng-proxy/translation.h"
#include "util/Cast.hxx"

#include <inline/list.h>

#include <time.h>
#include <string.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <errno.h>

#define MAX_CACHE_CHECK 256
#define MAX_CACHE_WFU 256

struct TranslateCacheItem {
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
        const char *param;
        const char *session;

        const struct sockaddr *local_address;
        size_t local_address_length;

        const char *remote_host;
        const char *host;
        const char *accept_language;
        const char *user_agent;
        const char *ua_class;
        const char *query_string;

        bool want;
    } request;

    TranslateResponse response;

    GRegex *regex, *inverse_regex;

    static TranslateCacheItem *FromPerHostSibling(list_head *lh) {
        return ContainerCast(lh, TranslateCacheItem, per_host_siblings);
    }
};

struct TranslateCachePerHost {
    /**
     * A double-linked list of #TranslateCacheItems (by its attribute
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

struct TranslateCacheRequest {
    struct pool *pool;

    struct tcache *tcache;

    const TranslateRequest *request;

    /** are we looking for a "BASE" cache entry? */
    bool find_base;

    const char *key;

    const TranslateHandler *handler;
    void *handler_ctx;
};

static const GRegexCompileFlags default_regex_compile_flags =
    GRegexCompileFlags(G_REGEX_MULTILINE|G_REGEX_DOTALL|
                       G_REGEX_RAW|G_REGEX_NO_AUTO_CAPTURE|
                       G_REGEX_OPTIMIZE);

#ifdef CACHE_LOG
#include <daemon/log.h>
#define cache_log(...) daemon_log(__VA_ARGS__)
#else
#define cache_log(...) do {} while (0)
#endif

static void
tcache_add_per_host(struct tcache *tcache, TranslateCacheItem *item)
{
    assert(translate_response_vary_contains(&item->response, TRANSLATE_HOST));

    const char *host = item->request.host;
    if (host == nullptr)
        host = "";

    TranslateCachePerHost *per_host = (TranslateCachePerHost *)
        hashmap_get(tcache->per_host, host);
    if (per_host == nullptr) {
        per_host = NewFromPool<TranslateCachePerHost>(tcache->pool);
        list_init(&per_host->items);
        per_host->tcache = tcache;
        per_host->host = p_strdup(tcache->pool, host);

        hashmap_add(tcache->per_host, per_host->host, per_host);
    }

    list_add(&item->per_host_siblings, &per_host->items);
}

static void
tcache_remove_per_host(TranslateCacheItem *item)
{
    assert(!list_empty(&item->per_host_siblings));
    assert(translate_response_vary_contains(&item->response, TRANSLATE_HOST));

    struct list_head *next = item->per_host_siblings.next;
    list_remove(&item->per_host_siblings);

    if (list_empty(next)) {
        /* if the next item is now empty, this can only be the
           per_host object - delete it */
        TranslateCachePerHost *per_host = (TranslateCachePerHost *)next;

        const char *host = item->request.host;
        if (host == nullptr)
            host = "";

        assert(strcmp(per_host->host, host) == 0);

        struct tcache *tcache = per_host->tcache;

        hashmap_remove_existing(tcache->per_host, host, per_host);

        p_free(tcache->pool, per_host->host);
        p_free(tcache->pool, per_host);
    }
}

static const char *
tcache_uri_key(struct pool *pool, const char *uri, const char *host,
               http_status_t status,
               const struct strref *check,
               const struct strref *want_full_uri,
               bool want)
{
    const char *key = status != 0
        ? p_sprintf(pool, "ERR%u_%s", status, uri)
        : uri;

    if (host != nullptr)
        /* workaround for a scalability problem in a large hosting
           environment: include the Host request header in the cache
           key */
        key = p_strcat(pool, host, ":", key, nullptr);

    if (check != nullptr && !strref_is_null(check)) {
        char buffer[MAX_CACHE_CHECK * 3];
        size_t length = uri_escape(buffer, check->data, check->length, '%');

        key = p_strncat(pool,
                        "|CHECK=", (size_t)7,
                        buffer, length,
                        key, strlen(key),
                        nullptr);
    }

    if (want_full_uri != nullptr && !strref_is_null(want_full_uri)) {
        char buffer[MAX_CACHE_WFU * 3];
        size_t length = uri_escape(buffer, want_full_uri->data,
                                   want_full_uri->length, '%');

        key = p_strncat(pool,
                        "|WFU=", (size_t)5,
                        buffer, length,
                        key, strlen(key),
                        nullptr);
    }

    if (want)
        key = p_strcat(pool, "|W_", key, nullptr);

    return key;
}

static const char *
tcache_request_key(struct pool *pool, const TranslateRequest *request)
{
    return request->uri != nullptr
        ? tcache_uri_key(pool, request->uri, request->host,
                         request->error_document_status,
                         &request->check, &request->want_full_uri,
                         !request->want.IsEmpty())
        : request->widget_type;
}

/* check whether the request could produce a cacheable response */
static bool
tcache_request_evaluate(const TranslateRequest *request)
{
    return (request->uri != nullptr || request->widget_type != nullptr) &&
        request->check.length < MAX_CACHE_CHECK &&
        request->want_full_uri.length <= MAX_CACHE_WFU &&
        request->authorization == nullptr;
}

/* check whether the response is cacheable */
static bool
tcache_response_evaluate(const TranslateResponse *response)
{
    assert(response != nullptr);

    return response->max_age != 0 &&
        response->www_authenticate == nullptr &&
        response->authentication_info == nullptr;
}

/**
 * Returns the string that shall be used for (inverse) regex matching.
 */
static const char *
tcache_regex_input(const char *uri, const TranslateResponse &response)
{
    assert(uri != nullptr);

    if (response.regex_tail) {
        assert(response.base != nullptr);
        assert(response.regex != nullptr ||
               response.inverse_regex != nullptr);

        uri = require_base_tail(uri, response.base);
    }

    return uri;
}

/**
 * Expand EXPAND_PATH_INFO specifications in all #resource_address
 * instances.
 */
static bool
tcache_expand_response(struct pool *pool, TranslateResponse *response,
                       const TranslateCacheItem *item, const char *uri,
                       GError **error_r)
{
    assert(pool != nullptr);
    assert(response != nullptr);
    assert(item != nullptr);

    if (uri == nullptr || item->regex == nullptr)
        return true;

    assert(response->regex != nullptr);
    assert(response->base != nullptr);

    GMatchInfo *match_info;
    if (!g_regex_match(item->regex,
                       tcache_regex_input(uri, *response),
                       GRegexMatchFlags(0), &match_info)) {
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
 * Copies the address #src to #dest and returns the new cache key.
 * Returns nullptr if the cache key should not be modified (i.e. if there
 * is no matching BASE packet).
 */
static const char *
tcache_store_address(struct pool *pool, struct resource_address *dest,
                     const struct resource_address *src,
                     const char *uri, const char *base,
                     bool easy_base, bool expandable)
{
    const char *tail = base_tail(uri, base);
    if (tail != nullptr) {
        /* we received a valid BASE packet - store only the base
           URI */

        if (easy_base || expandable) {
            /* when the response is expandable, skip appending the
               tail URI, don't call resource_address_save_base() */
            resource_address_copy(pool, dest, src);
            return p_strndup(pool, uri, tail - uri);
        }

        if (src->type == RESOURCE_ADDRESS_NONE) {
            /* _save_base() will fail on a "NONE" address, but in this
               case, the operation is useful and is allowed as a
               special case */
            dest->type = RESOURCE_ADDRESS_NONE;
            return p_strndup(pool, uri, tail - uri);
        }

        if (resource_address_save_base(pool, dest, src, tail) != nullptr)
            return p_strndup(pool, uri, tail - uri);
    }

    resource_address_copy(pool, dest, src);
    return nullptr;
}

static const char *
tcache_store_response(struct pool *pool, TranslateResponse *dest,
                      const TranslateResponse *src,
                      const TranslateRequest *request)
{
    const char *base = src->base;
    char *new_base = nullptr;

    if (src->auto_base) {
        assert(base == nullptr);
        assert(request->uri != nullptr);

        base = new_base = resource_address_auto_base(pool, &src->address,
                                                     request->uri);
    }

    const char *key = tcache_store_address(pool, &dest->address, &src->address,
                                           request->uri, base,
                                           src->easy_base,
                                           translate_response_is_expandable(src));
    translate_response_copy(pool, dest, src);

    if (key == nullptr)
        /* the BASE value didn't match - clear it */
        dest->base = nullptr;
    else if (new_base != nullptr)
        dest->base = new_base;

    if (dest->uri != nullptr) {
        const char *tail = base_tail(request->uri, src->base);

        if (tail != nullptr) {
            size_t length = base_string(dest->uri, tail);
            dest->uri = length != (size_t)-1
                ? p_strndup(pool, dest->uri, length)
                : nullptr;
        }
    }

    if (key != nullptr)
        key = tcache_uri_key(pool, key, request->host,
                             request->error_document_status,
                             &request->check, &request->want_full_uri,
                             !request->want.IsEmpty());

    return key;
}

/**
 * Load an address from a cached translate_response object, and apply
 * any BASE changes (if a BASE is present).
 */
static bool
tcache_load_address(struct pool *pool, const char *uri,
                    struct resource_address *dest,
                    const TranslateResponse *src,
                    GError **error_r)
{
    if (src->base != nullptr && !translate_response_is_expandable(src)) {
        const char *tail = require_base_tail(uri, src->base);

        if (!src->unsafe_base && !uri_path_verify_paranoid(tail - 1)) {
            g_set_error(error_r, http_response_quark(),
                        HTTP_STATUS_BAD_REQUEST, "Malformed URI");
            return false;
        }

        if (src->address.type == RESOURCE_ADDRESS_NONE) {
            /* see code comment in tcache_store_address() */
            dest->type = RESOURCE_ADDRESS_NONE;
            return true;
        }

        if (resource_address_load_base(pool, dest, &src->address,
                                       tail) != nullptr)
            return true;
    }

    resource_address_copy(pool, dest, &src->address);
    return true;
}

static bool
tcache_load_response(struct pool *pool, TranslateResponse *dest,
                     const TranslateResponse *src,
                     const char *uri, GError **error_r)
{
    if (!tcache_load_address(pool, uri, &dest->address, src, error_r))
        return false;

    translate_response_copy(pool, dest, src);
    return true;
}

static const char *
tcache_vary_copy(struct pool *pool, const char *p,
                 const TranslateResponse *response,
                 enum beng_translation_command command)
{
    return p != nullptr && translate_response_vary_contains(response, command)
        ? p_strdup(pool, p)
        : nullptr;
}

/**
 * @param strict in strict mode, nullptr values are a mismatch
 */
static bool
tcache_buffer_match(const void *a, size_t a_length,
                    const void *b, size_t b_length,
                    bool strict)
{
    assert((a == nullptr) == (a_length == 0));
    assert((b == nullptr) == (b_length == 0));

    if (a == nullptr || b == nullptr)
        return !strict && a == b;

    if (a_length != b_length)
        return false;

    return memcmp(a, b, a_length) == 0;
}

/**
 * @param strict in strict mode, nullptr values are a mismatch
 */
static bool
tcache_string_match(const char *a, const char *b, bool strict)
{
    if (a == nullptr || b == nullptr)
        return !strict && a == b;

    return strcmp(a, b) == 0;
}

/**
 * @param strict in strict mode, nullptr values are a mismatch
 */
static bool
tcache_uri_match(const char *a, const char *b, bool strict)
{
    if (a == nullptr || b == nullptr)
        return !strict && a == b;

    /* skip everything until the first slash; these may be prefixes
       added by tcache_uri_key() */
    a = strchr(a, '/');
    return a != nullptr && strcmp(a, b) == 0;
}

/**
 * @param strict in strict mode, unknown commands and nullptr values are
 * a mismatch
 */
static bool
tcache_vary_match(const TranslateCacheItem *item,
                  const TranslateRequest *request,
                  enum beng_translation_command command,
                  bool strict)
{
    switch (command) {
    case TRANSLATE_URI:
        return tcache_uri_match(item->item.key,
                                request->uri, strict);

    case TRANSLATE_PARAM:
        return tcache_string_match(item->request.param,
                                   request->param, strict);

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
    auto item = (const TranslateCacheItem *)_item;
    TranslateCacheRequest *tcr = (TranslateCacheRequest *)ctx;
    const TranslateRequest *request = tcr->request;

    if (tcr->find_base && item->response.base == nullptr)
        /* this is a "base" lookup, but this response does not contain
           a "BASE" packet */
        return false;

    if (item->response.base != nullptr && item->inverse_regex != nullptr &&
        g_regex_match(item->inverse_regex,
                      tcache_regex_input(request->uri, item->response),
                      GRegexMatchFlags(0), nullptr))
        /* the URI matches the inverse regular expression */
        return false;

    if (item->response.base != nullptr && item->regex != nullptr &&
        !g_regex_match(item->regex,
                       tcache_regex_input(request->uri, item->response),
                       GRegexMatchFlags(0), nullptr))
        /* the URI did not match the regular expression */
        return false;

    for (auto i : item->response.vary)
        if (!tcache_vary_match(item, request,
                               beng_translation_command(i),
                               false))
            return false;

    return true;
}

static TranslateCacheItem *
tcache_get(struct tcache *tcache, const TranslateRequest *request,
           const char *key, bool find_base)
{
    TranslateCacheRequest match_ctx = {
        .request = request,
        .find_base = find_base,
    };

    return (TranslateCacheItem *)cache_get_match(tcache->cache, key,
                                                 tcache_item_match, &match_ctx);
}

static TranslateCacheItem *
tcache_lookup(struct pool *pool, struct tcache *tcache,
              const TranslateRequest *request, const char *key)
{
    TranslateCacheItem *item = tcache_get(tcache, request, key, false);
    if (item != nullptr || request->uri == nullptr)
        return item;

    /* no match - look for matching BASE responses */

    char *uri = p_strdup(pool, key);
    char *slash = strrchr(uri, '/');

    if (slash != nullptr && slash[1] == 0) {
        /* if the URI already ends with a slash, don't repeat the
           original lookup - cut off this slash, and try again */
        *slash = 0;
        slash = strrchr(uri, '/');
    }

    while (slash != nullptr) {
        /* truncate string after slash */
        slash[1] = 0;

        item = tcache_get(tcache, request, uri, true);
        if (item != nullptr)
            return item;

        *slash = 0;
        slash = strrchr(uri, '/');
    }

    return nullptr;
}

struct tcache_invalidate_data {
    const TranslateRequest *request;

    ConstBuffer<uint16_t> vary;

    const char *site;
 };

static bool
tcache_invalidate_match(const struct cache_item *_item, void *ctx)
{
    const TranslateCacheItem *item = (const TranslateCacheItem *)_item;
    const tcache_invalidate_data *data = (const tcache_invalidate_data *)ctx;

    if (data->site != nullptr &&
        (item->response.site == nullptr || strcmp(data->site, item->response.site) != 0))
        return false;

    for (auto i : data->vary)
        if (!tcache_vary_match(item, data->request,
                               beng_translation_command(i),
                               true))
            return false;

    return true;
}

static unsigned
translate_cache_invalidate_host(struct tcache *tcache, const char *host)
{
    if (host == nullptr)
        host = "";

    TranslateCachePerHost *per_host = (TranslateCachePerHost *)
        hashmap_get(tcache->per_host, host);
    if (per_host == nullptr)
        return 0;

    assert(per_host->tcache == tcache);
    assert(strcmp(per_host->host, host) == 0);

    unsigned n_removed = 0;
    bool done;
    do {
        assert(!list_empty(&per_host->items));

        TranslateCacheItem *item =
            TranslateCacheItem::FromPerHostSibling(per_host->items.next);

        /* we're done when we're about to remove the last item - the
           last item will destroy the #TranslateCachePerHost object,
           so we need to check the condition before removing the cache
           item */
        done = item->per_host_siblings.next == &per_host->items;

        cache_remove_item(tcache->cache, item->item.key, &item->item);
        ++n_removed;
    } while (!done);

    return n_removed;
}

void
translate_cache_invalidate(struct tcache *tcache,
                           const TranslateRequest *request,
                           ConstBuffer<uint16_t> vary,
                           const char *site)
{
    struct tcache_invalidate_data data = {
        .request = request,
        .vary = vary,
        .site = site,
    };

    /* TODO: optimize the case when there is more than just
       TRANSLATE_HOST */
    gcc_unused
    unsigned removed = vary.size == 1 && vary.data[0] == TRANSLATE_HOST
        ? translate_cache_invalidate_host(tcache, request->host)
        : cache_remove_all_match(tcache->cache,
                                 tcache_invalidate_match, &data);
    cache_log(4, "translate_cache: invalidated %u cache items\n", removed);
}

static const TranslateCacheItem *
tcache_store(TranslateCacheRequest *tcr, const TranslateResponse *response)
{
    struct pool *pool = pool_new_slice(tcr->tcache->pool, "tcache_item",
                                       tcr->tcache->slice_pool);
    auto item = NewFromPool<TranslateCacheItem>(pool);
    unsigned max_age = response->max_age;

    cache_log(4, "translate_cache: store %s\n", tcr->key);

    if (max_age > 300)
        max_age = 300;

    cache_item_init_relative(&item->item, max_age, 1);
    item->pool = pool;

    item->request.param =
        tcache_vary_copy(pool, tcr->request->session,
                         response, TRANSLATE_PARAM);

    item->request.session =
        tcache_vary_copy(pool, tcr->request->session,
                         response, TRANSLATE_SESSION);

    item->request.local_address =
        tcr->request->local_address != nullptr &&
        translate_response_vary_contains(response, TRANSLATE_LOCAL_ADDRESS)
        ? (const struct sockaddr *)
        p_memdup(pool, tcr->request->local_address,
                 tcr->request->local_address_length)
        : nullptr;
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

    const char *key = tcache_store_response(pool, &item->response, response,
                                            tcr->request);
    if (key == nullptr)
        key = p_strdup(pool, tcr->key);

    if (response->regex != nullptr) {
        GRegexCompileFlags compile_flags = default_regex_compile_flags;
        if (translate_response_is_expandable(response))
            /* enable capturing if we need the match groups */
            compile_flags = GRegexCompileFlags(compile_flags &
                                               ~G_REGEX_NO_AUTO_CAPTURE);

        GError *error = nullptr;
        item->regex = g_regex_new(response->regex,
                                  compile_flags,
                                  GRegexMatchFlags(0), &error);
        if (item->regex == nullptr) {
            cache_log(2, "translate_cache: failed to compile regular expression: %s",
                      error->message);
            g_error_free(error);
        }
    } else
        item->regex = nullptr;

    if (response->inverse_regex != nullptr) {
        GError *error = nullptr;
        item->inverse_regex = g_regex_new(response->inverse_regex,
                                          default_regex_compile_flags,
                                          GRegexMatchFlags(0), &error);
        if (item->inverse_regex == nullptr) {
            cache_log(2, "translate_cache: failed to compile regular expression: %s",
                      error->message);
            g_error_free(error);
        }
    } else
        item->inverse_regex = nullptr;

    if (translate_response_vary_contains(response, TRANSLATE_HOST))
        tcache_add_per_host(tcr->tcache, item);
    else
        list_init(&item->per_host_siblings);

    cache_put_match(tcr->tcache->cache, key, &item->item,
                    tcache_item_match, tcr);

    return item;
}

/*
 * translate callback
 *
 */

static void
tcache_handler_response(const TranslateResponse *response, void *ctx)
{
    TranslateCacheRequest *tcr = (TranslateCacheRequest *)ctx;

    assert(response != nullptr);

    if (!response->invalidate.IsEmpty())
        translate_cache_invalidate(tcr->tcache, tcr->request,
                                   response->invalidate,
                                   nullptr);

    if (tcache_response_evaluate(response)) {
        auto item = tcache_store(tcr, response);

        if (tcr->request->uri != nullptr &&
            translate_response_is_expandable(response)) {
            /* create a writable copy and expand it */
            TranslateResponse *response2 = (TranslateResponse *)
                p_memdup(tcr->pool, response, sizeof(*response));

            GError *error = nullptr;
            if (!tcache_expand_response(tcr->pool, response2, item,
                                        tcr->request->uri, &error)) {
                tcr->handler->error(error, tcr->handler_ctx);
                return;
            }

            response = response2;
        } else if (response->easy_base) {
            /* create a writable copy and apply the BASE */
            auto response2 = NewFromPool<TranslateResponse>(tcr->pool);

            GError *error = nullptr;
            if (!tcache_load_response(tcr->pool, response2, response,
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
    TranslateCacheRequest *tcr = (TranslateCacheRequest *)ctx;

    cache_log(4, "translate_cache: error %s\n", tcr->key);

    tcr->handler->error(error, tcr->handler_ctx);
}

static const TranslateHandler tcache_handler = {
    .response = tcache_handler_response,
    .error = tcache_handler_error,
};

static void
tcache_hit(struct pool *pool, const char *uri, gcc_unused const char *key,
           const TranslateCacheItem *item,
           const TranslateHandler *handler, void *ctx)
{
    auto response = NewFromPool<TranslateResponse>(pool);

    cache_log(4, "translate_cache: hit %s\n", key);

    GError *error = nullptr;
    if (!tcache_load_response(pool, response, &item->response, uri, &error)) {
        handler->error(error, ctx);
        return;
    }

    if (uri != nullptr && translate_response_is_expandable(response) &&
        !tcache_expand_response(pool, response, item, uri, &error)) {
        handler->error(error, ctx);
        return;
    }

    handler->response(response, ctx);
}

static void
tcache_miss(struct pool *pool, struct tcache *tcache,
            const TranslateRequest *request, const char *key,
            const TranslateHandler *handler, void *ctx,
            struct async_operation_ref *async_ref)
{
    auto tcr = NewFromPool<TranslateCacheRequest>(pool);

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
tcache_validate_mtime(const TranslateResponse *response,
                      gcc_unused const char *key)
{
    if (response->validate_mtime.path == nullptr)
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
    TranslateCacheItem *item = (TranslateCacheItem *)_item;

    return tcache_validate_mtime(&item->response, item->item.key);
}

static void
tcache_destroy(struct cache_item *_item)
{
    TranslateCacheItem *item = (TranslateCacheItem *)_item;

    if (!list_empty(&item->per_host_siblings))
        tcache_remove_per_host(item);

    if (item->regex != nullptr)
        g_regex_unref(item->regex);

    if (item->inverse_regex != nullptr)
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
    tcache *tcache = NewFromPool<struct tcache>(pool);

    assert(stock != nullptr);

    tcache->pool = pool;
    tcache->slice_pool = slice_pool_new(2048, 65536);
    tcache->cache = cache_new(pool, &tcache_class, 65521, max_size);
    tcache->per_host = hashmap_new(pool, 3779);
    tcache->stock = stock;

    return tcache;
}

void
translate_cache_close(struct tcache *tcache)
{
    assert(tcache != nullptr);
    assert(tcache->cache != nullptr);
    assert(tcache->stock != nullptr);

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
                const TranslateRequest *request,
                const TranslateHandler *handler, void *ctx,
                struct async_operation_ref *async_ref)
{
    if (tcache_request_evaluate(request)) {
        const char *key = tcache_request_key(pool, request);
        TranslateCacheItem *item = tcache_lookup(pool, tcache, request, key);

        if (item != nullptr)
            tcache_hit(pool, request->uri, key, item, handler, ctx);
        else
            tcache_miss(pool, tcache, request, key, handler, ctx, async_ref);
    } else {
        cache_log(4, "translate_cache: ignore %s\n",
                  request->uri == nullptr
                  ? request->widget_type
                  : request->uri);

        tstock_translate(tcache->stock, pool,
                         request, handler, ctx, async_ref);
    }
}
