/*
 * Cache for translation server responses.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "tcache.hxx"
#include "tstock.hxx"
#include "TranslateHandler.hxx"
#include "translate_request.hxx"
#include "translate_response.hxx"
#include "translate_quark.hxx"
#include "regex.hxx"
#include "http_quark.h"
#include "http_domain.hxx"
#include "cache.hxx"
#include "stock.hxx"
#include "uri_base.hxx"
#include "uri_verify.hxx"
#include "uri_escape.hxx"
#include "puri_escape.hxx"
#include "tpool.hxx"
#include "pbuffer.hxx"
#include "paddress.hxx"
#include "SlicePool.hxx"
#include "load_file.hxx"
#include "util/djbhash.h"
#include "util/Error.hxx"
#include "beng-proxy/translation.h"

#include <boost/intrusive/list.hpp>
#include <boost/intrusive/unordered_set.hpp>

#include <time.h>
#include <string.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <errno.h>

#define MAX_CACHE_CHECK 256
#define MAX_CACHE_WFU 256
static constexpr size_t MAX_CONTENT_TYPE_LOOKUP = 256;
static constexpr size_t MAX_PROBE_PATH_SUFFIXES = 256;
static constexpr size_t MAX_FILE_NOT_FOUND = 256;
static constexpr size_t MAX_DIRECTORY_INDEX = 256;
static constexpr size_t MAX_READ_FILE = 256;

struct TranslateCachePerHost;
struct TranslateCachePerSite;

struct TranslateCacheItem {
    static constexpr auto link_mode = boost::intrusive::normal_link;
    typedef boost::intrusive::link_mode<link_mode> LinkMode;

    struct cache_item item;

    /**
     * A doubly linked list of cache items with the same HOST request
     * string.  Only those that had VARY=HOST in the response are
     * added to the list.  Check per_host!=nullptr to check whether
     * this item lives in such a list.
     */
    typedef boost::intrusive::list_member_hook<LinkMode> SiblingsHook;
    SiblingsHook per_host_siblings;
    TranslateCachePerHost *per_host;

    /**
     * A doubly linked list of cache items with the same SITE response
     * string.  Only those that had a #TRANSLATE_SITE packet in the
     * response are added to the list.  Check per_site!=nullptr to
     * check whether this item lives in such a list.
     */
    SiblingsHook per_site_siblings;
    TranslateCachePerSite *per_site;

    struct pool &pool;

    struct {
        const char *param;
        ConstBuffer<void> session;

        const char *listener_tag;
        SocketAddress local_address;

        const char *remote_host;
        const char *host;
        const char *accept_language;
        const char *user_agent;
        const char *ua_class;
        const char *query_string;

        ConstBuffer<void> internal_redirect;
        ConstBuffer<void> enotdir;

        const char *user;

        bool want;
    } request;

    TranslateResponse response;

    UniqueRegex regex, inverse_regex;

    TranslateCacheItem(struct pool &_pool)
        :per_host(nullptr),
         per_site(nullptr),
         pool(_pool) {}

    TranslateCacheItem(const TranslateCacheItem &) = delete;

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
                         const TranslateRequest &other_request) const {
        return VaryMatch(vary, other_request, true);
    }

    gcc_pure
    bool InvalidateMatch(ConstBuffer<uint16_t> vary,
                         const TranslateRequest &other_request,
                         const char *other_site) const {
        return (other_site == nullptr || MatchSite(other_site)) &&
            InvalidateMatch(vary, other_request);
    }
};

struct TranslateCachePerHost
    : boost::intrusive::unordered_set_base_hook<boost::intrusive::link_mode<boost::intrusive::normal_link>> {
    typedef boost::intrusive::member_hook<TranslateCacheItem,
                                          TranslateCacheItem::SiblingsHook,
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
                        ConstBuffer<uint16_t> vary);

    gcc_pure
    static size_t KeyHasher(const char *key) {
        assert(key != nullptr);

        return djb_hash_string(key);
    }

    gcc_pure
    static size_t ValueHasher(const TranslateCachePerHost &value) {
        return KeyHasher(value.host);
    }

    gcc_pure
    static bool KeyValueEqual(const char *a, const TranslateCachePerHost &b) {
        assert(a != nullptr);

        return strcmp(a, b.host) == 0;
    }

    struct Hash {
        gcc_pure
        size_t operator()(const TranslateCachePerHost &value) const {
            return ValueHasher(value);
        }
    };

    struct Equal {
        gcc_pure
        bool operator()(const TranslateCachePerHost &a,
                        const TranslateCachePerHost &b) const {
            return KeyValueEqual(a.host, b);
        }
    };
};

struct TranslateCachePerSite
    : boost::intrusive::unordered_set_base_hook<boost::intrusive::link_mode<boost::intrusive::normal_link>> {
    typedef boost::intrusive::member_hook<TranslateCacheItem,
                                          TranslateCacheItem::SiblingsHook,
                                          &TranslateCacheItem::per_site_siblings> MemberHook;
    typedef boost::intrusive::list<TranslateCacheItem, MemberHook,
                                   boost::intrusive::constant_time_size<false>> ItemList;

    /**
     * A double-linked list of #TranslateCacheItems (by its attribute
     * per_site_siblings).
     *
     * This must be the first attribute in the struct.
     */
    ItemList items;

    struct tcache &tcache;

    /**
     * A pointer to the hashmap key, for use with p_free().
     */
    const char *const site;

    TranslateCachePerSite(struct tcache &_tcache, const char *_site)
        :tcache(_tcache), site(_site) {
    }

    TranslateCachePerSite(const TranslateCachePerSite &) = delete;

    void Dispose();
    void Erase(TranslateCacheItem &item);

    unsigned Invalidate(const TranslateRequest &request,
                        ConstBuffer<uint16_t> vary);

    gcc_pure
    static size_t KeyHasher(const char *key) {
        assert(key != nullptr);

        return djb_hash_string(key);
    }

    gcc_pure
    static size_t ValueHasher(const TranslateCachePerSite &value) {
        return KeyHasher(value.site);
    }

    gcc_pure
    static bool KeyValueEqual(const char *a, const TranslateCachePerSite &b) {
        assert(a != nullptr);

        return strcmp(a, b.site) == 0;
    }

    struct Hash {
        gcc_pure
        size_t operator()(const TranslateCachePerSite &value) const {
            return ValueHasher(value);
        }
    };

    struct Equal {
        gcc_pure
        bool operator()(const TranslateCachePerSite &a,
                        const TranslateCachePerSite &b) const {
            return KeyValueEqual(a.site, b);
        }
    };
};

struct tcache {
    struct pool &pool;
    struct slice_pool &slice_pool;

    struct cache &cache;

    /**
     * This hash table maps each host name to a
     * #TranslateCachePerHost.  This is used to optimize the common
     * INVALIDATE=HOST response, to avoid traversing the whole cache.
     */
    typedef boost::intrusive::unordered_set<TranslateCachePerHost,
                                            boost::intrusive::hash<TranslateCachePerHost::Hash>,
                                            boost::intrusive::equal<TranslateCachePerHost::Equal>,
                                            boost::intrusive::constant_time_size<false>> PerHostSet;
    PerHostSet per_host;

    /**
     * This hash table maps each site name to a
     * #TranslateCachePerSite.  This is used to optimize the common
     * INVALIDATE=SITE response, to avoid traversing the whole cache.
     */
    typedef boost::intrusive::unordered_set<TranslateCachePerSite,
                                            boost::intrusive::hash<TranslateCachePerSite::Hash>,
                                            boost::intrusive::equal<TranslateCachePerSite::Equal>,
                                            boost::intrusive::constant_time_size<false>> PerSiteSet;
    PerSiteSet per_site;

    struct tstock &stock;

    /**
     * This flag may be set to false when initializing the translation
     * cache.  All responses will be regarded "non cacheable".  It
     * will be set to true as soon as the first response is received.
     */
    bool active;

    tcache(struct pool &_pool, struct tstock &_stock, unsigned max_size,
           bool handshake_cacheable);
    tcache(struct tcache &) = delete;

    ~tcache();

    TranslateCachePerHost &MakePerHost(const char *host);
    TranslateCachePerSite &MakePerSite(const char *site);

    unsigned InvalidateHost(const TranslateRequest &request,
                            ConstBuffer<uint16_t> vary);

    unsigned InvalidateSite(const TranslateRequest &request,
                            ConstBuffer<uint16_t> vary,
                            const char *site);
};

struct TranslateCacheRequest {
    struct pool *pool;

    struct tcache *tcache;

    const TranslateRequest &request;

    const bool cacheable;

    /** are we looking for a "BASE" cache entry? */
    const bool find_base;

    const char *key;

    const TranslateHandler *handler;
    void *handler_ctx;

    TranslateCacheRequest(struct pool &_pool, struct tcache &_tcache,
                          const TranslateRequest &_request, const char *_key,
                          bool _cacheable,
                          const TranslateHandler &_handler, void *_ctx)
        :pool(&_pool), tcache(&_tcache), request(_request),
         cacheable(_cacheable),
         find_base(false), key(_key),
         handler(&_handler), handler_ctx(_ctx) {}

    TranslateCacheRequest(const TranslateRequest &_request, bool _find_base)
        :request(_request), cacheable(true), find_base(_find_base) {}

    TranslateCacheRequest(TranslateCacheRequest &) = delete;
};

#ifdef CACHE_LOG
#include <daemon/log.h>
#define cache_log(...) daemon_log(__VA_ARGS__)
#else
#define cache_log(...) do {} while (0)
#endif

inline TranslateCachePerHost &
tcache::MakePerHost(const char *host)
{
    assert(host != nullptr);

    PerHostSet::insert_commit_data commit_data;
    auto result = per_host.insert_check(host, TranslateCachePerHost::KeyHasher,
                                        TranslateCachePerHost::KeyValueEqual,
                                        commit_data);
    if (!result.second)
        return *result.first;

    auto ph = NewFromPool<TranslateCachePerHost>(pool, *this,
                                                 p_strdup(&pool, host));
    per_host.insert_commit(*ph, commit_data);

    return *ph;
}

static void
tcache_add_per_host(struct tcache &tcache, TranslateCacheItem *item)
{
    assert(item->response.VaryContains(TRANSLATE_HOST));

    const char *host = item->request.host;
    if (host == nullptr)
        host = "";

    TranslateCachePerHost &per_host = tcache.MakePerHost(host);
    per_host.items.push_back(*item);
    item->per_host = &per_host;
}

void
TranslateCachePerHost::Dispose()
{
    assert(items.empty());

    tcache.per_host.erase(tcache.per_host.iterator_to(*this));

    p_free(&tcache.pool, host);
    DeleteFromPool(tcache.pool, this);
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

inline TranslateCachePerSite &
tcache::MakePerSite(const char *site)
{
    assert(site != nullptr);

    PerSiteSet::insert_commit_data commit_data;
    auto result = per_site.insert_check(site, TranslateCachePerSite::KeyHasher,
                                        TranslateCachePerSite::KeyValueEqual,
                                        commit_data);
    if (!result.second)
        return *result.first;

    auto ph = NewFromPool<TranslateCachePerSite>(pool, *this,
                                                 p_strdup(&pool, site));
    per_site.insert_commit(*ph, commit_data);

    return *ph;
}

static void
tcache_add_per_site(struct tcache &tcache, TranslateCacheItem *item)
{
    const char *site = item->response.site;
    assert(site != nullptr);

    TranslateCachePerSite &per_site = tcache.MakePerSite(site);
    per_site.items.push_back(*item);
    item->per_site = &per_site;
}

void
TranslateCachePerSite::Dispose()
{
    assert(items.empty());

    tcache.per_site.erase(tcache.per_site.iterator_to(*this));

    p_free(&tcache.pool, site);
    DeleteFromPool(tcache.pool, this);
}

void
TranslateCachePerSite::Erase(TranslateCacheItem &item)
{
    assert(item.per_site == this);
    assert(item.response.site != nullptr);

    items.erase(items.iterator_to(item));

    if (items.empty())
        Dispose();
}

static const char *
tcache_uri_key(struct pool &pool, const char *uri, const char *host,
               http_status_t status,
               ConstBuffer<void> check,
               ConstBuffer<void> want_full_uri,
               ConstBuffer<void> probe_path_suffixes,
               const char *probe_suffix,
               ConstBuffer<void> directory_index,
               ConstBuffer<void> file_not_found,
               ConstBuffer<void> read_file,
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

    if (!probe_path_suffixes.IsNull()) {
        char buffer[MAX_PROBE_PATH_SUFFIXES * 3];
        size_t length = uri_escape(buffer,
                                   (const char *)probe_path_suffixes.data,
                                   probe_path_suffixes.size);

        key = p_strncat(&pool,
                        buffer, length,
                        "=PPS", (size_t)4,
                        ":", size_t(probe_suffix != nullptr ? 1 : 0),
                        probe_suffix != nullptr ? probe_suffix : "",
                        probe_suffix != nullptr ? strlen(probe_suffix) : 0,
                        "]", (size_t)1,
                        key, strlen(key),
                        nullptr);
    } else {
        assert(probe_suffix == nullptr);
    }

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

    if (!read_file.IsNull()) {
        char buffer[MAX_READ_FILE * 3];
        size_t length = uri_escape(buffer, (const char *)read_file.data,
                                   read_file.size);

        key = p_strncat(&pool,
                        buffer, length,
                        "=RF]", (size_t)4,
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
                         request.probe_path_suffixes, request.probe_suffix,
                         request.directory_index,
                         request.file_not_found,
                         request.read_file,
                         !request.want.IsEmpty())
        : request.widget_type;
}

/* check whether the request could produce a cacheable response */
static bool
tcache_request_evaluate(const TranslateRequest &request)
{
    return (request.uri != nullptr || request.widget_type != nullptr ||
            tcache_is_content_type_lookup(request)) &&
        request.auth.IsNull() &&
        request.check.size < MAX_CACHE_CHECK &&
        request.want_full_uri.size <= MAX_CACHE_WFU &&
        request.probe_path_suffixes.size <= MAX_PROBE_PATH_SUFFIXES &&
        request.file_not_found.size <= MAX_FILE_NOT_FOUND &&
        request.directory_index.size <= MAX_DIRECTORY_INDEX &&
        request.read_file.size <= MAX_READ_FILE &&
        request.authorization == nullptr;
}

/* check whether the response is cacheable */
static bool
tcache_response_evaluate(const TranslateResponse &response)
{
    return response.max_age != 0 &&
        response.www_authenticate == nullptr &&
        response.authentication_info == nullptr;
}

/**
 * Returns the string that shall be used for (inverse) regex matching.
 */
static const char *
tcache_regex_input(struct pool *pool,
                   const char *uri, const char *host, const char *user,
                   const TranslateResponse &response,
                   bool inverse=false)
{
    assert(uri != nullptr);

    if (response.regex_tail) {
        assert(response.base != nullptr);
        assert(response.regex != nullptr ||
               response.inverse_regex != nullptr);

        uri = require_base_tail(uri, response.base);
    }

    if (inverse ? response.inverse_regex_unescape : response.regex_unescape) {
        assert(response.base != nullptr);
        assert(response.regex != nullptr ||
               response.inverse_regex != nullptr);

        uri = uri_unescape_dup(pool, uri, strlen(uri));
    }

    if (response.regex_on_host_uri) {
        if (*uri == '/')
            ++uri;
        uri = p_strcat(pool, host, "/", uri, nullptr);
    }

    if (response.regex_on_user_uri)
        uri = p_strcat(pool, user != nullptr ? user : "", "@", uri, nullptr);

    return uri;
}

/**
 * Expand EXPAND_PATH_INFO specifications in all #resource_address
 * instances.
 */
static bool
tcache_expand_response(struct pool &pool, TranslateResponse &response,
                       RegexPointer regex,
                       const char *uri, const char *host, const char *user,
                       Error &error)
{
    assert(regex.IsDefined());
    assert(uri != nullptr);

    assert(response.regex != nullptr);
    assert(response.base != nullptr);

    const AutoRewindPool auto_rewind(*tpool);

    if (response.regex_on_host_uri && strchr(host, '/') != nullptr) {
        error.Set(http_response_domain, HTTP_STATUS_BAD_REQUEST,
                  "Malformed Host header");
        return false;
    }

    uri = tcache_regex_input(tpool, uri, host, user, response);
    if (!response.unsafe_base && !uri_path_verify_paranoid(uri)) {
        error.Set(http_response_domain, HTTP_STATUS_BAD_REQUEST,
                  "Malformed URI");
        return false;
    }

    const auto match_info = regex.MatchCapture(uri);
    if (!match_info.IsDefined()) {
        /* shouldn't happen, as this has already been matched */
        error.Set(http_response_domain, HTTP_STATUS_BAD_REQUEST,
                  "Regex mismatch");
        return false;
    }

    return response.Expand(&pool, match_info, error);
}

static bool
tcache_expand_response(struct pool &pool, TranslateResponse &response,
                       RegexPointer regex,
                       const char *uri, const char *host, const char *user,
                       GError **error_r)
{
    Error error;
    bool success = tcache_expand_response(pool, response, regex,
                                          uri, host, user, error);
    if (!success) {
        GQuark quark = translate_quark();
        if (error.IsDomain(http_response_domain))
            quark = http_response_quark();

        g_set_error(error_r, quark, 0,
                    "translate_cache: %s", error.GetMessage());
    }

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
                         request.probe_path_suffixes, request.probe_suffix,
                         request.directory_index,
                         request.file_not_found,
                         request.read_file,
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

static bool
tcache_address_match(SocketAddress a, SocketAddress b, bool strict)
{
    return tcache_buffer_match(a.GetAddress(), a.GetSize(),
                               b.GetAddress(), b.GetSize(),
                               strict);
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

    case TRANSLATE_LISTENER_TAG:
        return tcache_string_match(request.listener_tag,
                                   other_request.listener_tag, strict);

    case TRANSLATE_LOCAL_ADDRESS:
    case TRANSLATE_LOCAL_ADDRESS_STRING:
        return tcache_address_match(request.local_address,
                                    other_request.local_address,
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

    case TRANSLATE_INTERNAL_REDIRECT:
        return tcache_buffer_match(request.internal_redirect,
                                   other_request.internal_redirect, strict);

    case TRANSLATE_ENOTDIR:
        return tcache_buffer_match(request.enotdir,
                                   other_request.enotdir, strict);

    case TRANSLATE_USER:
        return tcache_string_match(request.user,
                                   other_request.user, strict);

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

    const AutoRewindPool auto_rewind(*tpool);

    if (item.response.base != nullptr && item.inverse_regex.IsDefined() &&
        item.inverse_regex.Match(tcache_regex_input(tpool, request.uri, request.host,
                                                    request.user,
                                                    item.response, true)))
        /* the URI matches the inverse regular expression */
        return false;

    if (item.response.base != nullptr && item.regex.IsDefined() &&
        !item.regex.Match(tcache_regex_input(tpool, request.uri, request.host,
                                             request.user,
                                             item.response)))
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
                       ConstBuffer<uint16_t> vary)
{
    const char *host = request.host;
    if (host == nullptr)
        host = "";

    auto ph = per_host.find(host, TranslateCachePerHost::KeyHasher,
                            TranslateCachePerHost::KeyValueEqual);
    if (ph == per_host.end())
        return 0;

    assert(&ph->tcache == this);
    assert(strcmp(ph->host, host) == 0);

    return ph->Invalidate(request, vary);
}

inline unsigned
TranslateCachePerHost::Invalidate(const TranslateRequest &request,
                                  ConstBuffer<uint16_t> vary)
{
    unsigned n_removed = 0;

    items.remove_and_dispose_if([&request, vary](const TranslateCacheItem &item){
            return item.InvalidateMatch(vary, request);
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

inline unsigned
tcache::InvalidateSite(const TranslateRequest &request,
                       ConstBuffer<uint16_t> vary,
                       const char *site)
{
    assert(site != nullptr);

    auto ph = per_site.find(site, TranslateCachePerSite::KeyHasher,
                            TranslateCachePerSite::KeyValueEqual);
    if (ph == per_site.end())
        return 0;

    assert(&ph->tcache == this);
    assert(strcmp(ph->site, site) == 0);

    return ph->Invalidate(request, vary);
}

inline unsigned
TranslateCachePerSite::Invalidate(const TranslateRequest &request,
                                  ConstBuffer<uint16_t> vary)
{
    unsigned n_removed = 0;

    items.remove_and_dispose_if([&request, vary](const TranslateCacheItem &item){
            return item.InvalidateMatch(vary, request);
        },
        [&n_removed, this](TranslateCacheItem *item){
            assert(item->per_site == this);
            item->per_site = nullptr;

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
    unsigned removed = site != nullptr
        ? tcache.InvalidateSite(request, vary, site)
        : (vary.Contains(uint16_t(TRANSLATE_HOST))
           ? tcache.InvalidateHost(request, vary)
           : cache_remove_all_match(&tcache.cache,
                                    tcache_invalidate_match, &data));
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
    auto item = NewFromPool<TranslateCacheItem>(*pool, *pool);
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

    item->request.listener_tag =
        tcache_vary_copy(pool, tcr.request.listener_tag,
                         response, TRANSLATE_LISTENER_TAG);

    item->request.local_address =
        !tcr.request.local_address.IsNull() &&
        (response.VaryContains(TRANSLATE_LOCAL_ADDRESS) ||
         response.VaryContains(TRANSLATE_LOCAL_ADDRESS_STRING))
        ? DupAddress(*pool, tcr.request.local_address)
        : nullptr;

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
    item->request.internal_redirect =
        tcache_vary_copy(pool, tcr.request.internal_redirect,
                         response, TRANSLATE_INTERNAL_REDIRECT);
    item->request.enotdir =
        tcache_vary_copy(pool, tcr.request.enotdir,
                         response, TRANSLATE_ENOTDIR);
    item->request.user =
        tcache_vary_copy(pool, tcr.request.user,
                         response, TRANSLATE_USER);

    const char *key = tcache_store_response(*pool, item->response, response,
                                            tcr.request);
    if (item->response.base == nullptr && response.base != nullptr) {
        /* base mismatch - refuse to use this response */
        DeleteUnrefTrashPool(*pool, item);
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
        Error error;
        item->regex = response.CompileRegex(error);
        if (!item->regex.IsDefined()) {
            DeleteUnrefTrashPool(*pool, item);
            g_set_error(error_r, translate_quark(), 0,
                        "translate_cache: %s", error.GetMessage());
            return nullptr;
        }
    } else {
        assert(!response.IsExpandable());
    }

    if (response.inverse_regex != nullptr) {
        Error error;
        item->inverse_regex = response.CompileInverseRegex(error);
        if (!item->inverse_regex.IsDefined()) {
            DeleteUnrefTrashPool(*pool, item);
            g_set_error(error_r, translate_quark(), 0,
                        "translate_cache: %s", error.GetMessage());
            return nullptr;
        }
    }

    if (response.VaryContains(TRANSLATE_HOST))
        tcache_add_per_host(*tcr.tcache, item);

    if (response.site != nullptr)
        tcache_add_per_site(*tcr.tcache, item);

    cache_put_match(&tcr.tcache->cache, key, &item->item,
                    tcache_item_match, &tcr);

    return item;
}

/*
 * translate callback
 *
 */

static void
tcache_handler_response(TranslateResponse &response, void *ctx)
{
    TranslateCacheRequest &tcr = *(TranslateCacheRequest *)ctx;
    tcr.tcache->active = true;

    if (!response.invalidate.IsEmpty())
        translate_cache_invalidate(*tcr.tcache, tcr.request,
                                   response.invalidate,
                                   nullptr);

    RegexPointer regex;

    if (!tcr.cacheable) {
        cache_log(4, "translate_cache: ignore %s\n", tcr.key);
    } else if (tcache_response_evaluate(response)) {
        GError *error = nullptr;
        auto item = tcache_store(tcr, response, &error);
        if (item == nullptr) {
            tcr.handler->error(error, tcr.handler_ctx);
            return;
        }

        regex = item->regex;
    } else {
        cache_log(4, "translate_cache: nocache %s\n", tcr.key);
    }

    if (tcr.request.uri != nullptr && response.IsExpandable()) {
        UniqueRegex unref_regex;
        if (!regex.IsDefined()) {
            Error error;
            regex = unref_regex = response.CompileRegex(error);
            if (!regex.IsDefined()) {
                auto *gerror = g_error_new(translate_quark(), 0,
                                           "translate_cache: %s",
                                           error.GetMessage());
                tcr.handler->error(gerror, tcr.handler_ctx);
                return;
            }
        }

        GError *error = nullptr;
        bool success =
            tcache_expand_response(*tcr.pool, response, regex,
                                   tcr.request.uri, tcr.request.host,
                                   tcr.request.user,
                                   &error);

        if (!success) {
            tcr.handler->error(error, tcr.handler_ctx);
            return;
        }
    } else if (response.easy_base) {
        /* create a writable copy and apply the BASE */
        GError *error = nullptr;
        if (!response.CacheLoad(tcr.pool, response,
                                 tcr.request.uri, &error)) {
            tcr.handler->error(error, tcr.handler_ctx);
            return;
        }
    } else if (response.base != nullptr) {
        const char *uri = tcr.request.uri;
        const char *tail = require_base_tail(uri, response.base);
        if (!response.unsafe_base && !uri_path_verify_paranoid(tail)) {
            auto error = g_error_new(http_response_quark(),
                                     HTTP_STATUS_BAD_REQUEST,
                                     "Malformed URI");
            tcr.handler->error(error, tcr.handler_ctx);
            return;
        }
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
tcache_hit(struct pool &pool,
           const char *uri, const char *host, const char *user,
           gcc_unused const char *key,
           const TranslateCacheItem &item,
           const TranslateHandler &handler, void *ctx)
{
    auto response = NewFromPool<TranslateResponse>(pool);

    cache_log(4, "translate_cache: hit %s\n", key);

    GError *error = nullptr;
    if (!response->CacheLoad(&pool, item.response, uri, &error)) {
        handler.error(error, ctx);
        return;
    }

    if (uri != nullptr && response->IsExpandable() &&
        !tcache_expand_response(pool, *response, item.regex, uri, host, user,
                                &error)) {
        handler.error(error, ctx);
        return;
    }

    handler.response(*response, ctx);
}

static void
tcache_miss(struct pool &pool, struct tcache &tcache,
            const TranslateRequest &request, const char *key,
            bool cacheable,
            const TranslateHandler &handler, void *ctx,
            struct async_operation_ref &async_ref)
{
    auto tcr = NewFromPool<TranslateCacheRequest>(pool, pool, tcache,
                                                  request, key,
                                                  cacheable,
                                                  handler, ctx);

    if (cacheable)
        cache_log(4, "translate_cache: miss %s\n", key);

    tstock_translate(tcache.stock, pool,
                     request, tcache_handler, tcr, async_ref);
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
        if (errno == ENOENT && response.validate_mtime.mtime == 0) {
            /* the special value 0 matches when the file does not
               exist */
            cache_log(6, "translate_cache: [%s] validate_mtime enoent %s\n",
                      key, response.validate_mtime.path);
            return true;
        }

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

    if (item.per_site != nullptr)
        item.per_site->Erase(item);

    DeleteUnrefTrashPool(item.pool, &item);
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
tcache::tcache(struct pool &_pool, struct tstock &_stock, unsigned max_size,
               bool handshake_cacheable)
    :pool(_pool),
     slice_pool(*slice_pool_new(2048, 65536)),
     cache(*cache_new(_pool, &tcache_class, 65521, max_size)),
     per_host(PerHostSet::bucket_traits(PoolAlloc<PerHostSet::bucket_type>(_pool,
                                                                           3779),
                                        3779)),
     per_site(PerSiteSet::bucket_traits(PoolAlloc<PerSiteSet::bucket_type>(_pool,
                                                                           3779),
                                        3779)),
     stock(_stock), active(handshake_cacheable) {}

inline
tcache::~tcache()
{
    cache_close(&cache);
    slice_pool_free(&slice_pool);
}

struct tcache *
translate_cache_new(struct pool &_pool, struct tstock &stock,
                    unsigned max_size, bool handshake_cacheable)
{
    struct pool *pool = pool_new_libc(&_pool, "translate_cache");
    return NewFromPool<struct tcache>(*pool, *pool, stock, max_size,
                                      handshake_cacheable);
}

void
translate_cache_close(struct tcache *tcache)
{
    assert(tcache != nullptr);

    DeleteUnrefPool(tcache->pool, tcache);
}

struct cache_stats
translate_cache_get_stats(const struct tcache &tcache)
{
    return cache_get_stats(tcache.cache);
}

void
translate_cache_flush(struct tcache &tcache)
{
    cache_flush(&tcache.cache);
    slice_pool_compress(&tcache.slice_pool);
}


/*
 * methods
 *
 */

void
translate_cache(struct pool &pool, struct tcache &tcache,
                const TranslateRequest &request,
                const TranslateHandler &handler, void *ctx,
                struct async_operation_ref &async_ref)
{
    const bool cacheable = tcache.active && tcache_request_evaluate(request);
    const char *key = tcache_request_key(pool, request);
    TranslateCacheItem *item = cacheable
        ? tcache_lookup(pool, tcache, request, key)
        : nullptr;
    if (item != nullptr)
        tcache_hit(pool, request.uri, request.host, request.user, key,
                   *item, handler, ctx);
    else
        tcache_miss(pool, tcache, request, key, cacheable,
                    handler, ctx, async_ref);
}
