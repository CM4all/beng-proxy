/*
 * Cache for translation server responses.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "Cache.hxx"
#include "Stock.hxx"
#include "Handler.hxx"
#include "Request.hxx"
#include "Response.hxx"
#include "regex.hxx"
#include "http_quark.h"
#include "http_domain.hxx"
#include "HttpMessageResponse.hxx"
#include "cache.hxx"
#include "uri/uri_base.hxx"
#include "uri/uri_verify.hxx"
#include "uri/uri_escape.hxx"
#include "puri_escape.hxx"
#include "tpool.hxx"
#include "AllocatorPtr.hxx"
#include "pbuffer.hxx"
#include "paddress.hxx"
#include "SlicePool.hxx"
#include "AllocatorStats.hxx"
#include "load_file.hxx"
#include "util/djbhash.h"
#include "util/RuntimeError.hxx"
#include "util/StringView.hxx"
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

struct TranslateCacheItem final : CacheItem {
    static constexpr auto link_mode = boost::intrusive::normal_link;
    typedef boost::intrusive::link_mode<link_mode> LinkMode;

    /**
     * A doubly linked list of cache items with the same HOST request
     * string.  Only those that had VARY=HOST in the response are
     * added to the list.  Check per_host!=nullptr to check whether
     * this item lives in such a list.
     */
    typedef boost::intrusive::list_member_hook<LinkMode> SiblingsHook;
    SiblingsHook per_host_siblings;
    TranslateCachePerHost *per_host = nullptr;

    /**
     * A doubly linked list of cache items with the same SITE response
     * string.  Only those that had a #TRANSLATE_SITE packet in the
     * response are added to the list.  Check per_site!=nullptr to
     * check whether this item lives in such a list.
     */
    SiblingsHook per_site_siblings;
    TranslateCachePerSite *per_site = nullptr;

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

    TranslateCacheItem(struct pool &_pool, std::chrono::seconds max_age)
        :CacheItem(max_age, 1),
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

    /* virtual methods from class CacheItem */
    bool Validate() const override;
    void Destroy() override;
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
    SlicePool *const slice_pool;

    Cache *const cache;

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

    TranslateStock &stock;

    /**
     * This flag may be set to false when initializing the translation
     * cache.  All responses will be regarded "non cacheable".  It
     * will be set to true as soon as the first response is received.
     */
    bool active;

    static constexpr size_t N_BUCKETS = 3779;
    PerHostSet::bucket_type per_host_buckets[N_BUCKETS];
    PerSiteSet::bucket_type per_site_buckets[N_BUCKETS];

    tcache(struct pool &_pool, EventLoop &event_loop,
           TranslateStock &_stock, unsigned max_size,
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
        size_t length = uri_escape(buffer, check);

        key = p_strncat(&pool,
                        "|CHECK=", (size_t)7,
                        buffer, length,
                        key, strlen(key),
                        nullptr);
    }

    if (!want_full_uri.IsNull()) {
        char buffer[MAX_CACHE_WFU * 3];
        size_t length = uri_escape(buffer, want_full_uri);

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
        size_t length = uri_escape(buffer, probe_path_suffixes);

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
        size_t length = uri_escape(buffer, file_not_found);

        key = p_strncat(&pool,
                        buffer, length,
                        "=FNF]", (size_t)5,
                        key, strlen(key),
                        nullptr);
    }

    if (!directory_index.IsNull()) {
        char buffer[MAX_DIRECTORY_INDEX * 3];
        size_t length = uri_escape(buffer, directory_index);

        key = p_strncat(&pool,
                        buffer, length,
                        "=DIR]", (size_t)5,
                        key, strlen(key),
                        nullptr);
    }

    if (!read_file.IsNull()) {
        char buffer[MAX_READ_FILE * 3];
        size_t length = uri_escape(buffer, read_file);

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
    size_t length = uri_escape(buffer, request.content_type_lookup);
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
    return response.max_age != std::chrono::seconds::zero() &&
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

        uri = uri_unescape_dup(*pool, uri);
        if (uri == nullptr)
            return nullptr;
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
 *
 * Throws std::runtime_error on error.
 */
static void
tcache_expand_response(struct pool &pool, TranslateResponse &response,
                       RegexPointer regex,
                       const char *uri, const char *host, const char *user)
{
    assert(regex.IsDefined());
    assert(uri != nullptr);

    assert(response.regex != nullptr);
    assert(response.base != nullptr);

    const AutoRewindPool auto_rewind(*tpool);

    if (response.regex_on_host_uri && strchr(host, '/') != nullptr)
        throw HttpMessageResponse(HTTP_STATUS_BAD_REQUEST,
                                  "Malformed Host header");

    uri = tcache_regex_input(tpool, uri, host, user, response);
    if (uri == nullptr || (!response.unsafe_base &&
                           !uri_path_verify_paranoid(uri)))
        throw HttpMessageResponse(HTTP_STATUS_BAD_REQUEST,
                                  "Malformed URI");

    const auto match_info = regex.MatchCapture(uri);
    if (!match_info.IsDefined())
        /* shouldn't happen, as this has already been matched */
        throw HttpMessageResponse(HTTP_STATUS_BAD_REQUEST,
                                  "Regex mismatch");

    response.Expand(&pool, match_info);
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
tcache_vary_copy(struct pool &pool, ConstBuffer<T> value,
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
        return tcache_uri_match(key,
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
tcache_item_match(const CacheItem *_item, void *ctx)
{
    auto &item = *(const TranslateCacheItem *)_item;
    TranslateCacheRequest &tcr = *(TranslateCacheRequest *)ctx;
    const TranslateRequest &request = tcr.request;

    if (tcr.find_base && item.response.base == nullptr)
        /* this is a "base" lookup, but this response does not contain
           a "BASE" packet */
        return false;

    const AutoRewindPool auto_rewind(*tpool);

    if (item.response.base != nullptr && item.inverse_regex.IsDefined()) {
        auto input = tcache_regex_input(tpool, request.uri, request.host,
                                        request.user, item.response, true);
        if (input == nullptr || item.inverse_regex.Match(input))
            /* the URI matches the inverse regular expression */
            return false;
    }

    if (item.response.base != nullptr && item.regex.IsDefined()) {
        auto input = tcache_regex_input(tpool, request.uri, request.host,
                                        request.user, item.response);
        if (input == nullptr || !item.regex.Match(input))
            return false;
    }

    return item.VaryMatch(request, false);
}

static TranslateCacheItem *
tcache_get(struct tcache &tcache, const TranslateRequest &request,
           const char *key, bool find_base)
{
    assert(tcache.cache != nullptr);

    TranslateCacheRequest match_ctx(request, find_base);

    return (TranslateCacheItem *)
        tcache.cache->GetMatch(key, tcache_item_match, &match_ctx);
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
tcache_invalidate_match(const CacheItem *_item, void *ctx)
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
    assert(tcache.cache != nullptr);

    unsigned n_removed = 0;

    items.remove_and_dispose_if([&request, vary](const TranslateCacheItem &item){
            return item.InvalidateMatch(vary, request);
        },
        [&n_removed, this](TranslateCacheItem *item){
            assert(item->per_host == this);
            item->per_host = nullptr;

            tcache.cache->Remove(*item);
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
    assert(tcache.cache != nullptr);

    unsigned n_removed = 0;

    items.remove_and_dispose_if([&request, vary](const TranslateCacheItem &item){
            return item.InvalidateMatch(vary, request);
        },
        [&n_removed, this](TranslateCacheItem *item){
            assert(item->per_site == this);
            item->per_site = nullptr;

            tcache.cache->Remove(*item);
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
    if (tcache.cache == nullptr)
        return;

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
           : tcache.cache->RemoveAllMatch(tcache_invalidate_match, &data));
    cache_log(4, "translate_cache: invalidated %u cache items\n", removed);
}

/**
 * Throws std::runtime_error on error.
 */
static const TranslateCacheItem *
tcache_store(TranslateCacheRequest &tcr, const TranslateResponse &response)
{
    assert(tcr.tcache->slice_pool != nullptr);
    assert(tcr.tcache->cache != nullptr);

    struct pool *pool = pool_new_slice(&tcr.tcache->pool, "tcache_item",
                                       tcr.tcache->slice_pool);

    auto max_age = response.max_age;
    constexpr std::chrono::seconds max_max_age = std::chrono::hours(24);
    if (max_age < std::chrono::seconds::zero() || max_age > max_max_age)
        /* limit to one day */
        max_age = max_max_age;

    auto item = NewFromPool<TranslateCacheItem>(*pool, *pool, max_age);

    item->request.param =
        tcache_vary_copy(pool, tcr.request.param,
                         response, TRANSLATE_PARAM);

    item->request.session =
        tcache_vary_copy(*pool, tcr.request.session,
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
        tcache_vary_copy(*pool, tcr.request.internal_redirect,
                         response, TRANSLATE_INTERNAL_REDIRECT);
    item->request.enotdir =
        tcache_vary_copy(*pool, tcr.request.enotdir,
                         response, TRANSLATE_ENOTDIR);
    item->request.user =
        tcache_vary_copy(pool, tcr.request.user,
                         response, TRANSLATE_USER);

    const char *key = tcache_store_response(*pool, item->response, response,
                                            tcr.request);
    if (item->response.base == nullptr && response.base != nullptr) {
        /* base mismatch - refuse to use this response */
        DeleteUnrefTrashPool(*pool, item);
        throw HttpMessageResponse(HTTP_STATUS_BAD_REQUEST, "Base mismatch");
    }

    assert(!item->response.easy_base ||
           item->response.address.IsValidBase());

    if (key == nullptr)
        key = p_strdup(pool, tcr.key);

    cache_log(4, "translate_cache: store %s\n", key);

    if (response.regex != nullptr) {
        try {
            item->regex = response.CompileRegex();
        } catch (...) {
            DeleteUnrefTrashPool(*pool, item);
            throw;
        }
    } else {
        assert(!response.IsExpandable());
    }

    if (response.inverse_regex != nullptr) {
        try {
            item->inverse_regex = response.CompileInverseRegex();
        } catch (...) {
            DeleteUnrefTrashPool(*pool, item);
            throw;
        }
    }

    if (response.VaryContains(TRANSLATE_HOST))
        tcache_add_per_host(*tcr.tcache, item);

    if (response.site != nullptr)
        tcache_add_per_site(*tcr.tcache, item);

    tcr.tcache->cache->PutMatch(key, *item, tcache_item_match, &tcr);
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
        try {
            auto item = tcache_store(tcr, response);
            regex = item->regex;
        } catch (...) {
            tcr.handler->error(std::current_exception(), tcr.handler_ctx);
            return;
        }
    } else {
        cache_log(4, "translate_cache: nocache %s\n", tcr.key);
    }

    if (tcr.request.uri != nullptr && response.IsExpandable()) {
        UniqueRegex unref_regex;

        try {
            regex = unref_regex = response.CompileRegex();

            tcache_expand_response(*tcr.pool, response, regex,
                                   tcr.request.uri, tcr.request.host,
                                   tcr.request.user);
        } catch (...) {
            tcr.handler->error(std::current_exception(), tcr.handler_ctx);
            return;
        }
    } else if (response.easy_base) {
        /* create a writable copy and apply the BASE */
        try {
            response.CacheLoad(tcr.pool, response, tcr.request.uri);
        } catch (...) {
            tcr.handler->error(std::current_exception(), tcr.handler_ctx);
            return;
        }
    } else if (response.base != nullptr) {
        const char *uri = tcr.request.uri;
        const char *tail = require_base_tail(uri, response.base);
        if (!response.unsafe_base && !uri_path_verify_paranoid(tail)) {
            tcr.handler->error(std::make_exception_ptr(HttpMessageResponse(HTTP_STATUS_BAD_REQUEST,
                                                                           "Malformed URI")),
                               tcr.handler_ctx);
            return;
        }
    }

    tcr.handler->response(response, tcr.handler_ctx);
}

static void
tcache_handler_error(std::exception_ptr ep, void *ctx)
{
    TranslateCacheRequest &tcr = *(TranslateCacheRequest *)ctx;

    cache_log(4, "translate_cache: error %s\n", tcr.key);

    tcr.handler->error(ep, tcr.handler_ctx);
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

    try {
        response->CacheLoad(&pool, item.response, uri);
    } catch (...) {
        handler.error(std::current_exception(), ctx);
        return;
    }

    if (uri != nullptr && response->IsExpandable()) {
        try {
            tcache_expand_response(pool, *response, item.regex,
                                   uri, host, user);
        } catch (...) {
            handler.error(std::current_exception(), ctx);
            return;
        }
    }

    handler.response(*response, ctx);
}

static void
tcache_miss(struct pool &pool, struct tcache &tcache,
            const TranslateRequest &request, const char *key,
            bool cacheable,
            const TranslateHandler &handler, void *ctx,
            CancellablePointer &cancel_ptr)
{
    auto tcr = NewFromPool<TranslateCacheRequest>(pool, pool, tcache,
                                                  request, key,
                                                  cacheable,
                                                  handler, ctx);

    if (cacheable)
        cache_log(4, "translate_cache: miss %s\n", key);

    tstock_translate(tcache.stock, pool,
                     request, tcache_handler, tcr, cancel_ptr);
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

bool
TranslateCacheItem::Validate() const
{
    return tcache_validate_mtime(response, key);
}

void
TranslateCacheItem::Destroy()
{
    if (per_host != nullptr)
        per_host->Erase(*this);

    if (per_site != nullptr)
        per_site->Erase(*this);

    DeleteUnrefTrashPool(pool, this);
}

/*
 * constructor
 *
 */

inline
tcache::tcache(struct pool &_pool, EventLoop &event_loop,
               TranslateStock &_stock, unsigned max_size,
               bool handshake_cacheable)
    :pool(*pool_new_libc(&_pool, "translate_cache")),
     slice_pool(max_size > 0
                ? slice_pool_new(4096, 32768)
                : nullptr),
     cache(max_size > 0
           ? new Cache(event_loop, 65521, max_size)
           : nullptr),
     per_host(PerHostSet::bucket_traits(per_host_buckets, N_BUCKETS)),
     per_site(PerSiteSet::bucket_traits(per_site_buckets, N_BUCKETS)),
     stock(_stock), active(handshake_cacheable) {}

inline
tcache::~tcache()
{
    delete cache;
    if (slice_pool != nullptr)
        slice_pool_free(slice_pool);
    pool_unref(&pool);
}

struct tcache *
translate_cache_new(struct pool &pool, EventLoop &event_loop,
                    TranslateStock &stock,
                    unsigned max_size, bool handshake_cacheable)
{
    return new tcache(pool, event_loop, stock, max_size, handshake_cacheable);
}

void
translate_cache_close(struct tcache *tcache)
{
    assert(tcache != nullptr);

    delete tcache;
}

void
translate_cache_fork_cow(struct tcache &cache, bool inherit)
{
    if (cache.slice_pool != nullptr)
        slice_pool_fork_cow(*cache.slice_pool, inherit);
}

AllocatorStats
translate_cache_get_stats(const struct tcache &tcache)
{
    return pool_children_stats(tcache.pool);
}

void
translate_cache_flush(struct tcache &tcache)
{
    if (tcache.cache != nullptr)
        tcache.cache->Flush();
    if (tcache.slice_pool != nullptr)
        slice_pool_compress(tcache.slice_pool);
}


/*
 * methods
 *
 */

void
translate_cache(struct pool &pool, struct tcache &tcache,
                const TranslateRequest &request,
                const TranslateHandler &handler, void *ctx,
                CancellablePointer &cancel_ptr)
{
    const bool cacheable = tcache.cache != nullptr &&
        tcache.active && tcache_request_evaluate(request);
    const char *key = tcache_request_key(pool, request);
    TranslateCacheItem *item = cacheable
        ? tcache_lookup(pool, tcache, request, key)
        : nullptr;
    if (item != nullptr)
        tcache_hit(pool, request.uri, request.host, request.user, key,
                   *item, handler, ctx);
    else
        tcache_miss(pool, tcache, request, key, cacheable,
                    handler, ctx, cancel_ptr);
}
