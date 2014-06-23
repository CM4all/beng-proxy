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
#include "cache.hxx"
#include "stock.h"
#include "hashmap.h"
#include "uri_base.hxx"
#include "uri-verify.h"
#include "uri_escape.hxx"
#include "tpool.h"
#include "pbuffer.hxx"
#include "slice.hxx"
#include "beng-proxy/translation.h"

#include <boost/intrusive/list.hpp>

#include <time.h>
#include <string.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <errno.h>

#define MAX_CACHE_CHECK 256
#define MAX_CACHE_WFU 256
static constexpr size_t MAX_CONTENT_TYPE_LOOKUP = 256;
static constexpr size_t MAX_FILE_NOT_FOUND = 256;
static constexpr size_t MAX_DIRECTORY_INDEX = 256;

struct TranslateCachePerHost;

struct TranslateCacheItem {
    static constexpr auto link_mode = boost::intrusive::normal_link;
    typedef boost::intrusive::link_mode<link_mode> LinkMode;

    struct cache_item item;

    /**
     * A double linked list of cache items with the same HOST request
     * string.  Only those that had VARY=HOST in the response are
     * added to the list.  Check per_host!=nullptr to check whether
     * this item lives in such a list.
     */
    typedef boost::intrusive::list_member_hook<LinkMode> PerHostSiblingsHook;
    PerHostSiblingsHook per_host_siblings;
    TranslateCachePerHost *per_host;

    struct pool &pool;

    struct {
        const char *param;
        ConstBuffer<void> session;

        const struct sockaddr *local_address;
        size_t local_address_length;

        const char *remote_host;
        const char *host;
        const char *accept_language;
        const char *user_agent;
        const char *ua_class;
        const char *query_string;

        ConstBuffer<void> enotdir;

        bool want;
    } request;

    TranslateResponse response;

    GRegex *regex, *inverse_regex;

    TranslateCacheItem(struct pool &_pool)
        :per_host(nullptr),
         pool(_pool),
         regex(nullptr), inverse_regex(nullptr) {}

    TranslateCacheItem(const TranslateCacheItem &) = delete;

    ~TranslateCacheItem() {
        if (regex != nullptr)
            g_regex_unref(regex);

        if (inverse_regex != nullptr)
            g_regex_unref(inverse_regex);
    }

    gcc_pure
    bool MatchSite(const char *_site) const {
        assert(_site != nullptr);

        return response.site != nullptr &&
            strcmp(_site, response.site) == 0;
    }

    gcc_pure
    bool VaryMatch(const TranslateRequest &request,
                   enum beng_translation_command command,
                   bool strict) const;

    gcc_pure
    bool VaryMatch(ConstBuffer<uint16_t> vary,
                   const TranslateRequest &other_request, bool strict) const {
        for (auto i : vary)
            if (!VaryMatch(other_request, beng_translation_command(i), strict))
                return false;

        return true;
    }

    gcc_pure
    bool VaryMatch(const TranslateRequest &other_request, bool strict) const {
        return VaryMatch(response.vary, other_request, strict);
    }

    gcc_pure
    bool InvalidateMatch(ConstBuffer<uint16_t> vary,
                         const TranslateRequest &other_request,
                         const char *other_site) const {
        return (other_site == nullptr || MatchSite(other_site)) &&
            VaryMatch(vary, other_request, true);
    }
};

struct TranslateCachePerHost {
    typedef boost::intrusive::member_hook<TranslateCacheItem,
                                          TranslateCacheItem::PerHostSiblingsHook,
                                          &TranslateCacheItem::per_host_siblings> MemberHook;
    typedef boost::intrusive::list<TranslateCacheItem, MemberHook,
                                   boost::intrusive::constant_time_size<false>> ItemList;

    /**
     * A double-linked list of #TranslateCacheItems (by its attribute
     * per_host_siblings).
     *
     * This must be the first attribute in the struct.
     */
    ItemList items;

    struct tcache &tcache;

    /**
     * A pointer to the hashmap key, for use with p_free().
     */
    const char *const host;

    TranslateCachePerHost(struct tcache &_tcache, const char *_host)
        :tcache(_tcache), host(_host) {
    }

    TranslateCachePerHost(const TranslateCachePerHost &) = delete;

    void Dispose();
    void Erase(TranslateCacheItem &item);

    unsigned Invalidate(const TranslateRequest &request,
                        ConstBuffer<uint16_t> vary,
                        const char *site);
};

struct tcache {
    struct pool &pool;
    struct slice_pool &slice_pool;

    struct cache &cache;

    /**
     * This hash table maps each host name to a #tcache_per.  This is
     * used to optimize the common INVALIDATE=HOST response, to avoid
     * traversing the whole cache.
     */
    struct hashmap &per_host;

    struct tstock &stock;

    tcache(struct pool &_pool, struct tstock &_stock, unsigned max_size);
    tcache(struct tcache &) = delete;

    ~tcache();

    unsigned InvalidateHost(const TranslateRequest &request,
                            ConstBuffer<uint16_t> vary,
                            const char *site);
};

struct TranslateCacheRequest {
    struct pool *pool;

    struct tcache *tcache;

    const TranslateRequest &request;

    /** are we looking for a "BASE" cache entry? */
    const bool find_base;

    const char *key;

    const TranslateHandler *handler;
    void *handler_ctx;

    TranslateCacheRequest(struct pool &_pool, struct tcache &_tcache,
                          const TranslateRequest &_request, const char *_key,
                          const TranslateHandler &_handler, void *_ctx)
        :pool(&_pool), tcache(&_tcache), request(_request),
         find_base(false), key(_key),
         handler(&_handler), handler_ctx(_ctx) {}

    TranslateCacheRequest(const TranslateRequest &_request, bool _find_base)
        :request(_request), find_base(_find_base) {}

    TranslateCacheRequest(TranslateCacheRequest &) = delete;
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
tcache_add_per_host(struct tcache &tcache, TranslateCacheItem *item)
{
    assert(item->response.VaryContains(TRANSLATE_HOST));

    const char *host = item->request.host;
    if (host == nullptr)
        host = "";

    TranslateCachePerHost *per_host = (TranslateCachePerHost *)
        hashmap_get(&tcache.per_host, host);
    if (per_host == nullptr) {
        per_host = NewFromPool<TranslateCachePerHost>(&tcache.pool,
                                                      tcache,
                                                      p_strdup(&tcache.pool, host));

        hashmap_add(&tcache.per_host, per_host->host, per_host);
    }

    per_host->items.push_back(*item);
    item->per_host = per_host;
}

void
TranslateCachePerHost::Dispose()
{
    assert(items.empty());

    hashmap_remove_existing(&tcache.per_host, host, this);

    p_free(&tcache.pool, host);
    DeleteFromPool(&tcache.pool, this);
}

void
TranslateCachePerHost::Erase(TranslateCacheItem &item)
{
    assert(item.per_host == this);
    assert(item.response.VaryContains(TRANSLATE_HOST));

    items.erase(items.iterator_to(item));

    if (items.empty())
        Dispose();
}

static const char *
tcache_uri_key(struct pool &pool, const char *uri, const char *host,
               http_status_t status,
               ConstBuffer<void> check,
               ConstBuffer<void> want_full_uri,
               ConstBuffer<void> directory_index,
               ConstBuffer<void> file_not_found,
               bool want)
{
    const char *key = status != 0
        ? p_sprintf(&pool, "ERR%u_%s", status, uri)
        : uri;

    if (host != nullptr)
        /* workaround for a scalability problem in a large hosting
           environment: include the Host request header in the cache
           key */
        key = p_strcat(&pool, host, ":", key, nullptr);

    if (!check.IsNull()) {
        char buffer[MAX_CACHE_CHECK * 3];
        size_t length = uri_escape(buffer, (const char *)check.data,
                                   check.size);

        key = p_strncat(&pool,
                        "|CHECK=", (size_t)7,
                        buffer, length,
                        key, strlen(key),
                        nullptr);
    }

    if (!want_full_uri.IsNull()) {
        char buffer[MAX_CACHE_WFU * 3];
        size_t length = uri_escape(buffer, (const char *)want_full_uri.data,
                                   want_full_uri.size);

        key = p_strncat(&pool,
                        "|WFU=", (size_t)5,
                        buffer, length,
                        key, strlen(key),
                        nullptr);
    }

    if (want)
        key = p_strcat(&pool, "|W_", key, nullptr);

    if (!file_not_found.IsNull()) {
        char buffer[MAX_FILE_NOT_FOUND * 3];
        size_t length = uri_escape(buffer, (const char *)file_not_found.data,
                                   file_not_found.size);

        key = p_strncat(&pool,
                        buffer, length,
                        "=FNF]", (size_t)5,
                        key, strlen(key),
                        nullptr);
    }

    if (!directory_index.IsNull()) {
        char buffer[MAX_DIRECTORY_INDEX * 3];
        size_t length = uri_escape(buffer, (const char *)directory_index.data,
                                   directory_index.size);

        key = p_strncat(&pool,
                        buffer, length,
                        "=DIR]", (size_t)5,
                        key, strlen(key),
                        nullptr);
    }

    return key;
}

static bool
tcache_is_content_type_lookup(const TranslateRequest &request)
{
    return !request.content_type_lookup.IsNull() &&
        request.content_type_lookup.size <= MAX_CONTENT_TYPE_LOOKUP &&
        request.suffix != nullptr;
}

static const char *
tcache_content_type_lookup_key(struct pool *pool,
                               const TranslateRequest &request)
{
    char buffer[MAX_CONTENT_TYPE_LOOKUP * 3];
    size_t length = uri_escape(buffer,
                               (const char *)request.content_type_lookup.data,
                               request.content_type_lookup.size);
    return p_strncat(pool, "CTL|", size_t(4),
                     buffer, length,
                     "|", size_t(1),
                     request.suffix, strlen(request.suffix),
                     nullptr);
}

static const char *
tcache_request_key(struct pool &pool, const TranslateRequest &request)
{
    if (tcache_is_content_type_lookup(request))
        return tcache_content_type_lookup_key(&pool, request);

    return request.uri != nullptr
        ? tcache_uri_key(pool, request.uri, request.host,
                         request.error_document_status,
                         request.check, request.want_full_uri,
                         request.directory_index,
                         request.file_not_found,
                         !request.want.IsEmpty())
        : request.widget_type;
}

/* check whether the request could produce a cacheable response */
static bool
tcache_request_evaluate(const TranslateRequest *request)
{
    return (request->uri != nullptr || request->widget_type != nullptr ||
            tcache_is_content_type_lookup(*request)) &&
        request->auth.IsNull() &&
        request->check.size < MAX_CACHE_CHECK &&
        request->want_full_uri.size <= MAX_CACHE_WFU &&
        request->file_not_found.size <= MAX_FILE_NOT_FOUND &&
        request->directory_index.size <= MAX_DIRECTORY_INDEX &&
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
tcache_regex_input(struct pool *pool, const char *uri,
                   const TranslateResponse &response)
{
    assert(uri != nullptr);

    if (response.regex_tail) {
        assert(response.base != nullptr);
        assert(response.regex != nullptr ||
               response.inverse_regex != nullptr);

        uri = require_base_tail(uri, response.base);
    }

    if (response.regex_unescape) {
        assert(response.base != nullptr);
        assert(response.regex != nullptr ||
               response.inverse_regex != nullptr);

        uri = uri_unescape_dup(pool, uri, strlen(uri));
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

    const AutoRewindPool auto_rewind(tpool);

    uri = tcache_regex_input(tpool, uri, *response);
    if (!response->unsafe_base && !uri_path_verify_paranoid(uri)) {
        g_set_error(error_r, http_response_quark(),
                    HTTP_STATUS_BAD_REQUEST, "Malformed URI");
        return false;
    }

    GMatchInfo *match_info;
    if (!g_regex_match(item->regex, uri,
                       GRegexMatchFlags(0), &match_info)) {
        /* shouldn't happen, as this has already been matched */
        g_set_error(error_r, http_response_quark(),
                    HTTP_STATUS_BAD_REQUEST, "Regex mismatch");
        return false;
    }

    bool success = response->Expand(pool, match_info, error_r);
    g_match_info_free(match_info);
    return success;
}

static const char *
tcache_store_response(struct pool &pool, TranslateResponse &dest,
                      const TranslateResponse &src,
                      const TranslateRequest &request)
{
    const bool has_base = dest.CacheStore(&pool, src, request.uri);
    return has_base
        /* generate a new cache key for the BASE */
        ? tcache_uri_key(pool, dest.base, request.host,
                         request.error_document_status,
                         request.check, request.want_full_uri,
                         request.directory_index,
                         request.file_not_found,
                         !request.want.IsEmpty())
        /* no BASE, cache key unmodified */
        : nullptr;
}

static const char *
tcache_vary_copy(struct pool *pool, const char *p,
                 const TranslateResponse &response,
                 enum beng_translation_command command)
{
    return p != nullptr && response.VaryContains(command)
        ? p_strdup(pool, p)
        : nullptr;
}

template<typename T>
static ConstBuffer<T>
tcache_vary_copy(struct pool *pool, ConstBuffer<T> value,
                 const TranslateResponse &response,
                 enum beng_translation_command command)
{
    return !value.IsNull() && response.VaryContains(command)
        ? DupBuffer(pool, value)
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
tcache_buffer_match(ConstBuffer<void> a, ConstBuffer<void> b, bool strict)
{
    if (a.IsNull() || b.IsNull())
        return !strict && a.data == b.data;

    return a.size == b.size && memcmp(a.data, b.data, a.size) == 0;
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
bool
TranslateCacheItem::VaryMatch(const TranslateRequest &other_request,
                              enum beng_translation_command command,
                              bool strict) const
{
    switch (command) {
    case TRANSLATE_URI:
        return tcache_uri_match(item.key,
                                other_request.uri, strict);

    case TRANSLATE_PARAM:
        return tcache_string_match(request.param,
                                   other_request.param, strict);

    case TRANSLATE_SESSION:
        return tcache_buffer_match(request.session,
                                   other_request.session, strict);

    case TRANSLATE_LOCAL_ADDRESS:
    case TRANSLATE_LOCAL_ADDRESS_STRING:
        return tcache_buffer_match(request.local_address,
                                   request.local_address_length,
                                   other_request.local_address,
                                   other_request.local_address_length,
                                   strict);

    case TRANSLATE_REMOTE_HOST:
        return tcache_string_match(request.remote_host,
                                   other_request.remote_host, strict);

    case TRANSLATE_HOST:
        return tcache_string_match(request.host, other_request.host, strict);

    case TRANSLATE_LANGUAGE:
        return tcache_string_match(request.accept_language,
                                   other_request.accept_language, strict);

    case TRANSLATE_USER_AGENT:
        return tcache_string_match(request.user_agent,
                                   other_request.user_agent, strict);

    case TRANSLATE_UA_CLASS:
        return tcache_string_match(request.ua_class,
                                   other_request.ua_class, strict);

    case TRANSLATE_QUERY_STRING:
        return tcache_string_match(request.query_string,
                                   other_request.query_string, strict);

    case TRANSLATE_ENOTDIR:
        return tcache_buffer_match(request.enotdir,
                                   other_request.enotdir, strict);

    default:
        return !strict;
    }
}

static bool
tcache_item_match(const struct cache_item *_item, void *ctx)
{
    auto &item = *(const TranslateCacheItem *)_item;
    TranslateCacheRequest &tcr = *(TranslateCacheRequest *)ctx;
    const TranslateRequest &request = tcr.request;

    if (tcr.find_base && item.response.base == nullptr)
        /* this is a "base" lookup, but this response does not contain
           a "BASE" packet */
        return false;

    const AutoRewindPool auto_rewind(tpool);

    if (item.response.base != nullptr && item.inverse_regex != nullptr &&
        g_regex_match(item.inverse_regex,
                      tcache_regex_input(tpool, request.uri, item.response),
                      GRegexMatchFlags(0), nullptr))
        /* the URI matches the inverse regular expression */
        return false;

    if (item.response.base != nullptr && item.regex != nullptr &&
        !g_regex_match(item.regex,
                       tcache_regex_input(tpool, request.uri, item.response),
                       GRegexMatchFlags(0), nullptr))
        /* the URI did not match the regular expression */
        return false;

    return item.VaryMatch(request, false);
}

static TranslateCacheItem *
tcache_get(struct tcache &tcache, const TranslateRequest &request,
           const char *key, bool find_base)
{
    TranslateCacheRequest match_ctx(request, find_base);

    return (TranslateCacheItem *)cache_get_match(&tcache.cache, key,
                                                 tcache_item_match, &match_ctx);
}

static TranslateCacheItem *
tcache_lookup(struct pool &pool, struct tcache &tcache,
              const TranslateRequest &request, const char *key)
{
    TranslateCacheItem *item = tcache_get(tcache, request, key, false);
    if (item != nullptr || request.uri == nullptr)
        return item;

    /* no match - look for matching BASE responses */

    char *uri = p_strdup(&pool, key);
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
    const TranslateCacheItem &item = *(const TranslateCacheItem *)_item;
    const tcache_invalidate_data &data = *(const tcache_invalidate_data *)ctx;

    return item.InvalidateMatch(data.vary, *data.request, data.site);
}

inline unsigned
tcache::InvalidateHost(const TranslateRequest &request,
                       ConstBuffer<uint16_t> vary,
                       const char *site)
{
    const char *host = request.host;
    if (host == nullptr)
        host = "";

    TranslateCachePerHost *ph = (TranslateCachePerHost *)
        hashmap_get(&per_host, host);
    if (ph == nullptr)
        return 0;

    assert(&ph->tcache == this);
    assert(strcmp(ph->host, host) == 0);

    return ph->Invalidate(request, vary, site);
}

inline unsigned
TranslateCachePerHost::Invalidate(const TranslateRequest &request,
                                  ConstBuffer<uint16_t> vary,
                                  const char *site)
{
    unsigned n_removed = 0;

    items.remove_and_dispose_if([&request, vary, site](const TranslateCacheItem &item){
            return item.InvalidateMatch(vary, request, site);
        },
        [&n_removed, this](TranslateCacheItem *item){
            assert(item->per_host == this);
            item->per_host = nullptr;

            cache_remove_item(&tcache.cache, item->item.key, &item->item);
            ++n_removed;
        });

    if (items.empty())
        Dispose();

    return n_removed;
}

void
translate_cache_invalidate(struct tcache &tcache,
                           const TranslateRequest &request,
                           ConstBuffer<uint16_t> vary,
                           const char *site)
{
    struct tcache_invalidate_data data = {
        .request = &request,
        .vary = vary,
        .site = site,
    };

    gcc_unused
    unsigned removed = vary.Contains(uint16_t(TRANSLATE_HOST))
        ? tcache.InvalidateHost(request, vary, site)
        : cache_remove_all_match(&tcache.cache,
                                 tcache_invalidate_match, &data);
    cache_log(4, "translate_cache: invalidated %u cache items\n", removed);
}

/**
 * @return nullptr on error
 */
static const TranslateCacheItem *
tcache_store(TranslateCacheRequest &tcr, const TranslateResponse &response,
             GError **error_r)
{
    assert(error_r == nullptr || *error_r == nullptr);

    struct pool *pool = pool_new_slice(&tcr.tcache->pool, "tcache_item",
                                       &tcr.tcache->slice_pool);
    auto item = NewFromPool<TranslateCacheItem>(pool, *pool);
    unsigned max_age = response.max_age;

    if (max_age > 86400)
        /* limit to one day */
        max_age = 86400;

    cache_item_init_relative(&item->item, max_age, 1);

    item->request.param =
        tcache_vary_copy(pool, tcr.request.param,
                         response, TRANSLATE_PARAM);

    item->request.session =
        tcache_vary_copy(pool, tcr.request.session,
                         response, TRANSLATE_SESSION);

    item->request.local_address =
        tcr.request.local_address != nullptr &&
        (response.VaryContains(TRANSLATE_LOCAL_ADDRESS) ||
         response.VaryContains(TRANSLATE_LOCAL_ADDRESS_STRING))
        ? (const struct sockaddr *)
        p_memdup(pool, tcr.request.local_address,
                 tcr.request.local_address_length)
        : nullptr;
    item->request.local_address_length =
        tcr.request.local_address_length;

    tcache_vary_copy(pool, tcr.request.remote_host,
                     response, TRANSLATE_REMOTE_HOST);
    item->request.remote_host =
        tcache_vary_copy(pool, tcr.request.remote_host,
                         response, TRANSLATE_REMOTE_HOST);
    item->request.host = tcache_vary_copy(pool, tcr.request.host,
                                          response, TRANSLATE_HOST);
    item->request.accept_language =
        tcache_vary_copy(pool, tcr.request.accept_language,
                         response, TRANSLATE_LANGUAGE);
    item->request.user_agent =
        tcache_vary_copy(pool, tcr.request.user_agent,
                         response, TRANSLATE_USER_AGENT);
    item->request.ua_class =
        tcache_vary_copy(pool, tcr.request.ua_class,
                         response, TRANSLATE_UA_CLASS);
    item->request.query_string =
        tcache_vary_copy(pool, tcr.request.query_string,
                         response, TRANSLATE_QUERY_STRING);
    item->request.enotdir =
        tcache_vary_copy(pool, tcr.request.enotdir,
                         response, TRANSLATE_ENOTDIR);

    const char *key = tcache_store_response(*pool, item->response, response,
                                            tcr.request);
    if (item->response.base == nullptr && response.base != nullptr) {
        /* base mismatch - refuse to use this response */
        DeleteFromPool(pool, item);
        pool_unref(pool);
        g_set_error(error_r, http_response_quark(),
                    HTTP_STATUS_BAD_REQUEST, "Base mismatch");
        return nullptr;
    }

    assert(!item->response.easy_base ||
           item->response.address.IsValidBase());

    if (key == nullptr)
        key = p_strdup(pool, tcr.key);

    cache_log(4, "translate_cache: store %s\n", key);

    if (response.regex != nullptr) {
        GRegexCompileFlags compile_flags = default_regex_compile_flags;
        if (response.IsExpandable())
            /* enable capturing if we need the match groups */
            compile_flags = GRegexCompileFlags(compile_flags &
                                               ~G_REGEX_NO_AUTO_CAPTURE);

        item->regex = g_regex_new(response.regex,
                                  compile_flags,
                                  GRegexMatchFlags(0), error_r);
        if (item->regex == nullptr) {
            DeleteFromPool(pool, item);
            pool_unref(pool);
            g_prefix_error(error_r,
                           "translate_cache: ");
            return nullptr;
        }
    }

    if (response.inverse_regex != nullptr) {
        item->inverse_regex = g_regex_new(response.inverse_regex,
                                          default_regex_compile_flags,
                                          GRegexMatchFlags(0), error_r);
        if (item->inverse_regex == nullptr) {
            DeleteFromPool(pool, item);
            pool_unref(pool);
            g_prefix_error(error_r,
                           "translate_cache: ");
            return nullptr;
        }
    }

    if (response.VaryContains(TRANSLATE_HOST))
        tcache_add_per_host(*tcr.tcache, item);

    cache_put_match(&tcr.tcache->cache, key, &item->item,
                    tcache_item_match, &tcr);

    return item;
}

/*
 * translate callback
 *
 */

static void
tcache_handler_response(TranslateResponse *response, void *ctx)
{
    TranslateCacheRequest &tcr = *(TranslateCacheRequest *)ctx;

    assert(response != nullptr);

    if (!response->invalidate.IsEmpty())
        translate_cache_invalidate(*tcr.tcache, tcr.request,
                                   response->invalidate,
                                   nullptr);

    if (tcache_response_evaluate(response)) {
        GError *error = nullptr;
        auto item = tcache_store(tcr, *response, &error);
        if (item == nullptr) {
            tcr.handler->error(error, tcr.handler_ctx);
            return;
        }

        if (tcr.request.uri != nullptr && response->IsExpandable()) {
            /* create a writable copy and expand it */
            if (!tcache_expand_response(tcr.pool, response, item,
                                        tcr.request.uri, &error)) {
                tcr.handler->error(error, tcr.handler_ctx);
                return;
            }
        } else if (response->easy_base) {
            /* create a writable copy and apply the BASE */
            if (!response->CacheLoad(tcr.pool, *response,
                                     tcr.request.uri, &error)) {
                tcr.handler->error(error, tcr.handler_ctx);
                return;
            }
        }
    } else {
        cache_log(4, "translate_cache: nocache %s\n", tcr.key);
    }

    tcr.handler->response(response, tcr.handler_ctx);
}

static void
tcache_handler_error(GError *error, void *ctx)
{
    TranslateCacheRequest &tcr = *(TranslateCacheRequest *)ctx;

    cache_log(4, "translate_cache: error %s\n", tcr.key);

    tcr.handler->error(error, tcr.handler_ctx);
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
    if (!response->CacheLoad(pool, item->response, uri, &error)) {
        handler->error(error, ctx);
        return;
    }

    if (uri != nullptr && response->IsExpandable() &&
        !tcache_expand_response(pool, response, item, uri, &error)) {
        handler->error(error, ctx);
        return;
    }

    handler->response(response, ctx);
}

static void
tcache_miss(struct pool &pool, struct tcache &tcache,
            const TranslateRequest &request, const char *key,
            const TranslateHandler &handler, void *ctx,
            struct async_operation_ref &async_ref)
{
    auto tcr = NewFromPool<TranslateCacheRequest>(&pool, pool, tcache,
                                                  request, key,
                                                  handler, ctx);

    cache_log(4, "translate_cache: miss %s\n", key);

    tstock_translate(&tcache.stock, &pool,
                     &request, &tcache_handler, tcr, &async_ref);
}

gcc_pure
static bool
tcache_validate_mtime(const TranslateResponse &response,
                      gcc_unused const char *key)
{
    if (response.validate_mtime.path == nullptr)
        return true;

    cache_log(6, "translate_cache: [%s] validate_mtime %llu %s\n",
              key, (unsigned long long)response.validate_mtime.mtime,
              response.validate_mtime.path);

    struct stat st;
    if (lstat(response.validate_mtime.path, &st) < 0) {
        cache_log(3, "translate_cache: [%s] failed to stat '%s': %s\n",
                  key, response.validate_mtime.path, g_strerror(errno));
        return false;
    }

    if (!S_ISREG(st.st_mode)) {
        cache_log(3, "translate_cache: [%s] not a regular file: %s\n",
                  key, response.validate_mtime.path);
        return false;
    }

    if (st.st_mtime == (time_t)response.validate_mtime.mtime) {
        cache_log(6, "translate_cache: [%s] validate_mtime unmodified %s\n",
                  key, response.validate_mtime.path);
        return true;
    } else {
        cache_log(5, "translate_cache: [%s] validate_mtime modified %s\n",
                  key, response.validate_mtime.path);
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

    return tcache_validate_mtime(item->response, item->item.key);
}

static void
tcache_destroy(struct cache_item *_item)
{
    TranslateCacheItem &item = *(TranslateCacheItem *)_item;

    if (item.per_host != nullptr)
        item.per_host->Erase(item);

    auto &pool = item.pool;
    DeleteFromPool(&pool, &item);
    pool_unref(&pool);
}

static const struct cache_class tcache_class = {
    .validate = tcache_validate,
    .destroy = tcache_destroy,
};


/*
 * constructor
 *
 */

inline
tcache::tcache(struct pool &_pool, struct tstock &_stock, unsigned max_size)
    :pool(_pool),
     slice_pool(*slice_pool_new(2048, 65536)),
     cache(*cache_new(&_pool, &tcache_class, 65521, max_size)),
     per_host(*hashmap_new(&_pool, 3779)),
     stock(_stock) {}

inline
tcache::~tcache()
{
    cache_close(&cache);
    slice_pool_free(&slice_pool);
}

struct tcache *
translate_cache_new(struct pool *pool, struct tstock *stock,
                    unsigned max_size)
{
    assert(stock != nullptr);

    pool = pool_new_libc(pool, "translate_cache");
    return NewFromPool<struct tcache>(pool, *pool, *stock, max_size);
}

void
translate_cache_close(struct tcache *tcache)
{
    assert(tcache != nullptr);

    auto &pool = tcache->pool;
    DeleteFromPool(&pool, tcache);
    pool_unref(&pool);
}

void
translate_cache_get_stats(const struct tcache *tcache,
                          struct cache_stats *data)
{
    cache_get_stats(&tcache->cache, data);
}

void
translate_cache_flush(struct tcache *tcache)
{
    cache_flush(&tcache->cache);
    slice_pool_compress(&tcache->slice_pool);
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
        const char *key = tcache_request_key(*pool, *request);
        TranslateCacheItem *item = tcache_lookup(*pool, *tcache, *request,
                                                 key);

        if (item != nullptr)
            tcache_hit(pool, request->uri, key, item, handler, ctx);
        else
            tcache_miss(*pool, *tcache, *request, key,
                        *handler, ctx, *async_ref);
    } else {
        cache_log(4, "translate_cache: ignore %s\n",
                  request->uri == nullptr
                  ? request->widget_type
                  : request->uri);

        tstock_translate(&tcache->stock, pool,
                         request, handler, ctx, async_ref);
    }
}
