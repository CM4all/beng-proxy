// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#include "Cache.hxx"
#include "Layout.hxx"
#include "translation/Handler.hxx"
#include "translation/Request.hxx"
#include "translation/Response.hxx"
#include "translation/Protocol.hxx"
#include "HttpMessageResponse.hxx"
#include "cache.hxx"
#include "http/Status.hxx"
#include "uri/Base.hxx"
#include "uri/Verify.hxx"
#include "uri/Escape.hxx"
#include "uri/PEscape.hxx"
#include "uri/PNormalize.hxx"
#include "pool/tpool.hxx"
#include "pool/Holder.hxx"
#include "AllocatorPtr.hxx"
#include "pool/StringBuilder.hxx"
#include "pool/PSocketAddress.hxx"
#include "memory/SlicePool.hxx"
#include "stats/CacheStats.hxx"
#include "lib/fmt/Unsafe.hxx"
#include "lib/pcre/UniqueRegex.hxx"
#include "io/Logger.hxx"
#include "util/djb_hash.hxx"
#include "util/IntrusiveHashSet.hxx"
#include "util/IntrusiveList.hxx"
#include "util/SpanCast.hxx"

#include <cassert>

#include <time.h>
#include <string.h>
#include <stdlib.h>
#include <fcntl.h> // for AT_*
#include <sys/stat.h>
#include <errno.h>

using std::string_view_literals::operator""sv;

static constexpr std::size_t MAX_CACHE_LAYOUT = 256;
static constexpr std::size_t MAX_CACHE_CHECK = 256;
static constexpr std::size_t MAX_CACHE_WFU = 256;
static constexpr std::size_t MAX_CONTENT_TYPE_LOOKUP = 256;
static constexpr std::size_t MAX_CHAIN = 256;
static constexpr std::size_t MAX_MOUNT_LISTEN_STREAM = 256;
static constexpr std::size_t MAX_PROBE_PATH_SUFFIXES = 256;
static constexpr std::size_t MAX_FILE_NOT_FOUND = 256;
static constexpr std::size_t MAX_DIRECTORY_INDEX = 256;
static constexpr std::size_t MAX_READ_FILE = 256;

struct TranslateCacheItem final : PoolHolder, CacheItem {
	IntrusiveHashSetHook<IntrusiveHookMode::AUTO_UNLINK> per_host_siblings, per_site_siblings;

	struct {
		const char *param;
		std::span<const std::byte> session;
		std::span<const std::byte> realm_session;

		const char *listener_tag;
		SocketAddress local_address;

		const char *remote_host;
		const char *host;
		const char *accept_language;
		const char *user_agent;
		const char *query_string;

		std::span<const std::byte> internal_redirect;
		std::span<const std::byte> enotdir;

		const char *user;

		bool want;
	} request;

	TranslateResponse response;

	UniqueRegex regex, inverse_regex;

	AllocatorStats stats;

	TranslateCacheItem(PoolPtr &&_pool,
			   std::chrono::steady_clock::time_point now,
			   std::chrono::seconds max_age) noexcept
		:PoolHolder(std::move(_pool)),
		 CacheItem(now, max_age, 1) {}

	TranslateCacheItem(const TranslateCacheItem &) = delete;

	using PoolHolder::GetPool;

	[[gnu::pure]]
	bool MatchSite(const char *_site) const noexcept {
		assert(_site != nullptr);

		return response.site != nullptr &&
			strcmp(_site, response.site) == 0;
	}

	[[gnu::pure]]
	bool VaryMatch(const TranslateRequest &request,
		       TranslationCommand command,
		       bool strict) const noexcept;

	[[gnu::pure]]
	bool VaryMatch(std::span<const TranslationCommand> vary,
		       const TranslateRequest &other_request, bool strict) const noexcept {
		for (auto i : vary)
			if (!VaryMatch(other_request, i, strict))
				return false;

		return true;
	}

	[[gnu::pure]]
	bool VaryMatch(const TranslateRequest &other_request, bool strict) const noexcept {
		return VaryMatch(response.vary, other_request, strict);
	}

	[[gnu::pure]]
	bool InvalidateMatch(std::span<const TranslationCommand> vary,
			     const TranslateRequest &other_request) const noexcept {
		return VaryMatch(vary, other_request, true);
	}

	[[gnu::pure]]
	bool InvalidateMatch(std::span<const TranslationCommand> vary,
			     const TranslateRequest &other_request,
			     const char *other_site) const noexcept {
		return (other_site == nullptr || MatchSite(other_site)) &&
			InvalidateMatch(vary, other_request);
	}

	/* virtual methods from class CacheItem */
	bool Validate() const noexcept override;
	void Destroy() noexcept override;

	struct StringViewHash {
		[[gnu::pure]]
		std::size_t operator()(std::string_view key) const noexcept {
			return djb_hash(AsBytes(key));
		}
	};

	struct GetHost {
		[[gnu::pure]]
		std::string_view operator()(const TranslateCacheItem &item) const noexcept {
			return item.request.host != nullptr
				? std::string_view{item.request.host}
				: std::string_view{};
		}
	};

	struct GetSite {
		[[gnu::pure]]
		std::string_view operator()(const TranslateCacheItem &item) const noexcept {
			assert(item.response.site != nullptr);

			return item.response.site;
		}
	};
};

struct tcache final : private CacheHandler {
	const PoolPtr pool;
	SlicePool slice_pool;

	static constexpr std::size_t N_BUCKETS = 128 * 1024;

	/**
	 * This hash table maps each host name to the
	 * #TranslateCacheItem instances with that host.  This is used
	 * to optimize the common INVALIDATE=HOST response, to avoid
	 * traversing the whole cache.
	 */
	using PerHostSet =
		IntrusiveHashSet<TranslateCacheItem, N_BUCKETS,
				 IntrusiveHashSetOperators<TranslateCacheItem,
							   TranslateCacheItem::GetHost,
							   TranslateCacheItem::StringViewHash,
							   std::equal_to<std::string_view>>,
				 IntrusiveHashSetMemberHookTraits<&TranslateCacheItem::per_host_siblings>>;
	PerHostSet per_host;

	/**
	 * This hash table maps each site name to the
	 * #TranslateCacheItem instances with that site.  This is used
	 * to optimize the common INVALIDATE=SITE response, to avoid
	 * traversing the whole cache.
	 */
	using PerSiteSet =
		IntrusiveHashSet<TranslateCacheItem, N_BUCKETS,
				 IntrusiveHashSetOperators<TranslateCacheItem,
							   TranslateCacheItem::GetSite,
							   TranslateCacheItem::StringViewHash,
							   std::equal_to<std::string_view>>,
				 IntrusiveHashSetMemberHookTraits<&TranslateCacheItem::per_site_siblings>>;
	PerSiteSet per_site;

	Cache cache;

	CacheStats stats{};

	TranslationService &next;

	/**
	 * This flag may be set to false when initializing the translation
	 * cache.  All responses will be regarded "non cacheable".  It
	 * will be set to true as soon as the first response is received.
	 */
	bool active;

	tcache(struct pool &_pool, EventLoop &event_loop,
	       TranslationService &_next, unsigned max_size,
	       bool handshake_cacheable);
	tcache(struct tcache &) = delete;

	~tcache() noexcept = default;

	unsigned InvalidateHost(const TranslateRequest &request,
				std::span<const TranslationCommand> vary) noexcept;

	unsigned InvalidateSite(const TranslateRequest &request,
				std::span<const TranslationCommand> vary,
				std::string_view site) noexcept;

	void Invalidate(const TranslateRequest &request,
			std::span<const TranslationCommand> vary,
			const char *site) noexcept;

private:
	/* virtual methods from CacheHandler */
	void OnCacheItemAdded(const CacheItem &_item) noexcept override {
		const auto &item = (const TranslateCacheItem &)_item;
		stats.allocator += item.stats;
	}

	void OnCacheItemRemoved(const CacheItem &_item) noexcept override {
		const auto &item = (const TranslateCacheItem &)_item;
		stats.allocator -= item.stats;
	}
};

struct TranslateCacheRequest final : TranslateHandler {
	const AllocatorPtr alloc;

	struct tcache *tcache;

	const TranslateRequest &request;

	const bool cacheable;

	/** are we looking for a "BASE" cache entry? */
	const bool find_base;

	const char *key;

	TranslateHandler *handler;

	TranslateCacheRequest(AllocatorPtr _alloc, struct tcache &_tcache,
			      const TranslateRequest &_request, const char *_key,
			      bool _cacheable,
			      TranslateHandler &_handler) noexcept
		:alloc(_alloc), tcache(&_tcache), request(_request),
		 cacheable(_cacheable),
		 find_base(false), key(_key),
		 handler(&_handler) {}

	TranslateCacheRequest(TranslateCacheRequest &) = delete;

	/* virtual methods from TranslateHandler */
	void OnTranslateResponse(UniquePoolPtr<TranslateResponse> response) noexcept override;
	void OnTranslateError(std::exception_ptr error) noexcept override;
};

static const char *
tcache_uri_key(AllocatorPtr alloc, const char *uri, const char *host,
	       HttpStatus status,
	       std::span<const std::byte> layout,
	       const TranslationLayoutItem *layout_item,
	       std::span<const std::byte> check,
	       const char *check_header,
	       std::span<const std::byte> want_full_uri,
	       std::span<const std::byte> probe_path_suffixes,
	       const char *probe_suffix,
	       std::span<const std::byte> directory_index,
	       std::span<const std::byte> file_not_found,
	       std::span<const std::byte> read_file,
	       bool path_exists,
	       bool want) noexcept
{
	PoolStringBuilder<256> b;

	char rf_buffer[MAX_READ_FILE * 3];
	if (read_file.data() != nullptr) {
		b.emplace_back(rf_buffer, UriEscape(rf_buffer, read_file));
		b.push_back("=RF]");
	}

	char di_buffer[MAX_DIRECTORY_INDEX * 3];
	if (directory_index.data() != nullptr) {
		b.emplace_back(di_buffer, UriEscape(di_buffer, directory_index));
		b.push_back("=DIR]");
	}

	char fnf_buffer[MAX_FILE_NOT_FOUND * 3];
	if (file_not_found.data() != nullptr) {
		b.emplace_back(fnf_buffer, UriEscape(fnf_buffer, file_not_found));
		b.push_back("=FNF]");
	}

	char pps_buffer[MAX_PROBE_PATH_SUFFIXES * 3];
	if (probe_path_suffixes.data() != nullptr) {
		b.emplace_back(pps_buffer,
			       UriEscape(pps_buffer, probe_path_suffixes));
		b.push_back("=PPS");

		if (probe_suffix != nullptr) {
			b.push_back(":");
			b.push_back(probe_suffix);
			b.push_back("]");
		}
	} else {
		assert(probe_suffix == nullptr);
	}

	if (path_exists)
		b.push_back("|PE_");

	if (want)
		b.push_back("|W_");

	char wfu_buffer[MAX_CACHE_WFU * 3];
	if (want_full_uri.data() != nullptr) {
		b.push_back("|WFU=");
		b.emplace_back(wfu_buffer, UriEscape(wfu_buffer, want_full_uri));
	}

	char layout_buffer[MAX_CACHE_LAYOUT * 3];
	if (layout.data() != nullptr) {
		b.emplace_back(layout_buffer, UriEscape(layout_buffer, layout));

		if (layout_item != nullptr) {
			switch (layout_item->GetType()) {
			case TranslationLayoutItem::Type::BASE:
				b.push_back("--");
				break;

			case TranslationLayoutItem::Type::REGEX:
				b.push_back("-~");
				break;
			}

			b.emplace_back(layout_item->value);
		}

		b.push_back("=L|");
	} else {
		assert(layout_item == nullptr);
	}

	char check_buffer[MAX_CACHE_CHECK * 3];
	if (check.data() != nullptr) {
		b.push_back("|CHECK=");
		b.emplace_back(check_buffer, UriEscape(check_buffer, check));
	}

	if (check_header != nullptr) {
		b.push_back("|CH=");
		b.push_back(check_header);
	}

	if (host != nullptr) {
		/* workaround for a scalability problem in a large hosting
		   environment: include the Host request header in the cache
		   key */
		b.push_back(host);
		b.push_back(":");
	}

	char status_buffer[32];
	if (status != HttpStatus{})
		b.push_back(FmtUnsafeSV(status_buffer, "ERR{}_"sv, static_cast<unsigned>(status)));

	b.push_back(uri);

	return b(alloc);
}

[[gnu::pure]]
static bool
tcache_is_content_type_lookup(const TranslateRequest &request) noexcept
{
	return request.content_type_lookup.data() != nullptr &&
		request.content_type_lookup.size() <= MAX_CONTENT_TYPE_LOOKUP &&
		request.suffix != nullptr;
}

[[gnu::pure]]
static const char *
tcache_content_type_lookup_key(AllocatorPtr alloc,
			       const TranslateRequest &request) noexcept
{
	char buffer[MAX_CONTENT_TYPE_LOOKUP * 3];
	std::size_t length = UriEscape(buffer, request.content_type_lookup);
	return alloc.Concat("CTL|",
			    std::string_view{buffer, length},
			    '|',
			    request.suffix);
}

static const char *
tcache_chain_key(AllocatorPtr alloc, const TranslateRequest &request) noexcept
{
	char buffer[MAX_CHAIN * 3];
	std::size_t length = UriEscape(buffer, request.chain);

	char status_buffer[32];
	std::string_view status{};
	if (unsigned(request.status) != 0)
		status = FmtUnsafeSV(status_buffer, "{}", unsigned(request.status));

	return alloc.Concat("CHAIN|",
			    std::string_view{buffer, length},
			    '=',
			    status);
}

static const char *
tcache_mount_listen_stream_key(AllocatorPtr alloc, const TranslateRequest &request) noexcept
{
	char buffer[MAX_MOUNT_LISTEN_STREAM * 3];
	std::size_t length = UriEscape(buffer, request.mount_listen_stream);
	return alloc.Concat("MLS|"sv, std::string_view{buffer, length});
}

[[gnu::pure]]
static const char *
tcache_request_key(AllocatorPtr alloc, const TranslateRequest &request) noexcept
{
	if (tcache_is_content_type_lookup(request))
		return tcache_content_type_lookup_key(alloc, request);

	if (request.chain.data() != nullptr)
		return tcache_chain_key(alloc, request);

	if (request.mount_listen_stream.data() != nullptr)
		return tcache_mount_listen_stream_key(alloc, request);

	return request.uri != nullptr
		? tcache_uri_key(alloc, request.uri, request.host,
				 request.status,
				 request.layout, request.layout_item,
				 request.check, request.check_header,
				 request.want_full_uri,
				 request.probe_path_suffixes, request.probe_suffix,
				 request.directory_index,
				 request.file_not_found,
				 request.read_file,
				 request.path_exists,
				 !request.want.empty())
		: request.widget_type;
}

/* check whether the request could produce a cacheable response */
[[gnu::pure]]
static bool
tcache_request_evaluate(const TranslateRequest &request) noexcept
{
	return (request.uri != nullptr || request.widget_type != nullptr ||
		request.chain.data() != nullptr ||
		tcache_is_content_type_lookup(request)) &&
		request.chain_header == nullptr &&
		request.recover_session == nullptr &&
		request.http_auth.data() == nullptr && // TODO: allow caching HTTP_AUTH
		request.token_auth.data() == nullptr && // TODO: allow caching TOKEN_AUTH
		request.auth.data() == nullptr &&
		request.mount_listen_stream.size() < MAX_MOUNT_LISTEN_STREAM &&
		request.check.size() < MAX_CACHE_CHECK &&
		request.want_full_uri.size() <= MAX_CACHE_WFU &&
		request.probe_path_suffixes.size() <= MAX_PROBE_PATH_SUFFIXES &&
		request.file_not_found.size() <= MAX_FILE_NOT_FOUND &&
		request.directory_index.size() <= MAX_DIRECTORY_INDEX &&
		request.read_file.size() <= MAX_READ_FILE &&
		request.authorization == nullptr;
}

/* check whether the response is cacheable */
[[gnu::pure]]
static bool
tcache_response_evaluate(const TranslateResponse &response) noexcept
{
	return response.max_age != std::chrono::seconds::zero() &&
		response.www_authenticate == nullptr &&
		response.authentication_info == nullptr;
}

/**
 * Returns the string that shall be used for (inverse) regex matching.
 */
static const char *
tcache_regex_input(AllocatorPtr alloc,
		   const char *uri, const char *host, const char *user,
		   const TranslateResponse &response,
		   bool inverse=false) noexcept
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

		uri = uri_unescape_dup(alloc, uri);
		if (uri == nullptr)
			return nullptr;
	}

	if (response.regex_on_host_uri) {
		if (*uri == '/')
			++uri;
		uri = alloc.Concat(host, '/', uri);
	}

	if (response.regex_on_user_uri)
		uri = alloc.Concat(user != nullptr ? user : "", '@', uri);

	return uri;
}

/**
 * Expand EXPAND_PATH_INFO specifications in all #resource_address
 * instances.
 *
 * Throws std::runtime_error on error.
 */
static void
tcache_expand_response(AllocatorPtr alloc, TranslateResponse &response,
		       RegexPointer regex,
		       const char *uri, const char *host, const char *user)
{
	assert(regex.IsDefined());
	assert(uri != nullptr);

	assert(response.regex != nullptr);
	assert(response.base != nullptr);

	const TempPoolLease tpool;

	if (response.regex_on_host_uri && strchr(host, '/') != nullptr)
		throw HttpMessageResponse(HttpStatus::BAD_REQUEST,
					  "Malformed Host header");

	uri = tcache_regex_input(AllocatorPtr{tpool}, uri, host, user, response);
	if (!response.regex_raw)
		uri = NormalizeUriPath(AllocatorPtr{tpool}, uri);
	if (uri == nullptr || (!response.unsafe_base &&
			       !uri_path_verify_paranoid(uri)))
		throw HttpMessageResponse(HttpStatus::BAD_REQUEST,
					  "Malformed URI");

	const auto match_data = regex.Match(uri);
	if (!match_data)
		/* shouldn't happen, as this has already been matched */
		throw HttpMessageResponse(HttpStatus::BAD_REQUEST,
					  "Regex mismatch");

	response.Expand(alloc, match_data);
}

static const char *
tcache_store_response(AllocatorPtr alloc, TranslateResponse &dest,
		      const TranslateResponse &src,
		      const TranslateRequest &request)
{
	dest.CacheStore(alloc, src, request.uri);
	return dest.base != nullptr
		/* generate a new cache key for the BASE */
		? tcache_uri_key(alloc, dest.base, request.host,
				 request.status,
				 request.layout, request.layout_item,
				 request.check, request.check_header,
				 request.want_full_uri,
				 request.probe_path_suffixes, request.probe_suffix,
				 request.directory_index,
				 request.file_not_found,
				 request.read_file,
				 request.path_exists,
				 !request.want.empty())
		/* no BASE, cache key unmodified */
		: nullptr;
}

static const char *
tcache_vary_copy(AllocatorPtr alloc, const char *p,
		 const TranslateResponse &response,
		 TranslationCommand command) noexcept
{
	return p != nullptr && response.VaryContains(command)
		? alloc.Dup(p)
		: nullptr;
}

template<typename T>
static std::span<const T>
tcache_vary_copy(AllocatorPtr alloc, std::span<const T> value,
		 const TranslateResponse &response,
		 TranslationCommand command) noexcept
{
	return value.data() != nullptr && response.VaryContains(command)
		? alloc.Dup(value)
		: std::span<const T>{};
}

/**
 * @param strict in strict mode, nullptr values are a mismatch
 */
static bool
tcache_string_match(const char *a, const char *b, bool strict) noexcept
{
	if (a == nullptr || b == nullptr)
		return !strict && a == b;

	return strcmp(a, b) == 0;
}

/**
 * @param strict in strict mode, nullptr values are a mismatch
 */
[[gnu::pure]]
static bool
tcache_buffer_match(std::span<const std::byte> a, std::span<const std::byte> b,
		    bool strict) noexcept
{
	if (a.data() == nullptr || b.data() == nullptr)
		return !strict && a.data() == b.data();

	return a.size() == b.size() &&
		memcmp(a.data(), b.data(), a.size()) == 0;
}

static bool
tcache_address_match(SocketAddress a, SocketAddress b, bool strict) noexcept
{
	return tcache_buffer_match(a, b, strict);
}

/**
 * @param strict in strict mode, nullptr values are a mismatch
 */
static bool
tcache_uri_match(const char *a, const char *b, bool strict) noexcept
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
			      TranslationCommand command,
			      bool strict) const noexcept
{
	switch (command) {
	case TranslationCommand::URI:
		return tcache_uri_match(GetKey(),
					other_request.uri, strict);

	case TranslationCommand::PARAM:
		return tcache_string_match(request.param,
					   other_request.param, strict);

	case TranslationCommand::SESSION:
		return tcache_buffer_match(request.session,
					   other_request.session, strict);

	case TranslationCommand::REALM_SESSION:
		return tcache_buffer_match(request.realm_session,
					   other_request.realm_session,
					   strict);

	case TranslationCommand::LISTENER_TAG:
		return tcache_string_match(request.listener_tag,
					   other_request.listener_tag, strict);

	case TranslationCommand::LOCAL_ADDRESS:
	case TranslationCommand::LOCAL_ADDRESS_STRING:
		return tcache_address_match(request.local_address,
					    other_request.local_address,
					    strict);

	case TranslationCommand::REMOTE_HOST:
		return tcache_string_match(request.remote_host,
					   other_request.remote_host, strict);

	case TranslationCommand::HOST:
		return tcache_string_match(request.host, other_request.host, strict);

	case TranslationCommand::LANGUAGE:
		return tcache_string_match(request.accept_language,
					   other_request.accept_language, strict);

	case TranslationCommand::USER_AGENT:
		return tcache_string_match(request.user_agent,
					   other_request.user_agent, strict);

	case TranslationCommand::QUERY_STRING:
		return tcache_string_match(request.query_string,
					   other_request.query_string, strict);

	case TranslationCommand::INTERNAL_REDIRECT:
		return tcache_buffer_match(request.internal_redirect,
					   other_request.internal_redirect, strict);

	case TranslationCommand::ENOTDIR_:
		return tcache_buffer_match(request.enotdir,
					   other_request.enotdir, strict);

	case TranslationCommand::USER:
		return tcache_string_match(request.user,
					   other_request.user, strict);

	default:
		return !strict;
	}
}

struct TranslateCacheMatchContext {
	const TranslateRequest &request;
	const bool find_base;
};

static bool
tcache_item_match(const CacheItem *_item, void *ctx) noexcept
{
	auto &item = *(const TranslateCacheItem *)_item;
	auto &tcr = *(TranslateCacheMatchContext *)ctx;
	const TranslateRequest &request = tcr.request;

	if (tcr.find_base && item.response.base == nullptr)
		/* this is a "base" lookup, but this response does not contain
		   a "BASE" packet */
		return false;

	const TempPoolLease tpool;

	if (item.response.base != nullptr && item.inverse_regex.IsDefined()) {
		auto input = tcache_regex_input(AllocatorPtr{tpool},
						request.uri, request.host,
						request.user, item.response, true);
		if (input == nullptr || item.inverse_regex.Match(input))
			/* the URI matches the inverse regular expression */
			return false;
	}

	if (item.response.base != nullptr && item.regex.IsDefined()) {
		auto input = tcache_regex_input(AllocatorPtr{tpool},
						request.uri, request.host,
						request.user, item.response);
		if (input == nullptr || !item.regex.Match(input))
			return false;
	}

	return item.VaryMatch(request, false);
}

static TranslateCacheItem *
tcache_get(struct tcache &tcache, const TranslateRequest &request,
	   const char *key, bool find_base) noexcept
{
	TranslateCacheMatchContext match_ctx{request, find_base};

	return (TranslateCacheItem *)
		tcache.cache.GetMatch(key, tcache_item_match, &match_ctx);
}

static TranslateCacheItem *
tcache_lookup(AllocatorPtr alloc, struct tcache &tcache,
	      const TranslateRequest &request, const char *key) noexcept
{
	TranslateCacheItem *item = tcache_get(tcache, request, key, false);
	if (item != nullptr || request.uri == nullptr)
		return item;

	/* no match - look for matching BASE responses */

	char *uri = alloc.Dup(key);
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

struct TranslationCacheInvalidate {
	const TranslateRequest *request;

	std::span<const TranslationCommand> vary;

	const char *site;
};

static bool
tcache_invalidate_match(const CacheItem *_item, void *ctx) noexcept
{
	const TranslateCacheItem &item = *(const TranslateCacheItem *)_item;
	const auto &data = *(const TranslationCacheInvalidate *)ctx;

	return item.InvalidateMatch(data.vary, *data.request, data.site);
}

inline unsigned
tcache::InvalidateHost(const TranslateRequest &request,
		       std::span<const TranslationCommand> vary) noexcept
{
	const std::string_view host = request.host != nullptr
		? std::string_view{request.host}
		: std::string_view{};

	return per_host.remove_and_dispose_key_if(host, [&request, vary](const TranslateCacheItem &item){
		return item.InvalidateMatch(vary, request);
	}, [this](TranslateCacheItem *item){
		cache.Remove(*item);
	});
}

inline unsigned
tcache::InvalidateSite(const TranslateRequest &request,
		       std::span<const TranslationCommand> vary,
		       std::string_view site) noexcept
{
	return per_site.remove_and_dispose_key_if(site, [&request, vary](const TranslateCacheItem &item){
		return item.InvalidateMatch(vary, request);
	}, [this](TranslateCacheItem *item){
		cache.Remove(*item);
	});
}

void
tcache::Invalidate(const TranslateRequest &request,
		   std::span<const TranslationCommand> vary,
		   const char *site) noexcept
{
	TranslationCacheInvalidate data{&request, vary, site};

	[[maybe_unused]]
	unsigned removed = site != nullptr
		? InvalidateSite(request, vary, site)
		: (std::find(vary.begin(), vary.end(), TranslationCommand::HOST) != vary.end()
		   ? InvalidateHost(request, vary)
		   : cache.RemoveAllMatch(tcache_invalidate_match, &data));
	LogConcat(4, "TranslationCache", "invalidated ", removed, " cache items");
}

void
TranslationCache::Invalidate(const TranslateRequest &request,
			     std::span<const TranslationCommand> vary,
			     const char *site) noexcept
{
	cache->Invalidate(request, vary, site);
}

/**
 * Throws std::runtime_error on error.
 */
static const TranslateCacheItem *
tcache_store(TranslateCacheRequest &tcr, const TranslateResponse &response)
{
	auto max_age = response.max_age;
	constexpr std::chrono::seconds max_max_age = std::chrono::hours(24);
	if (max_age < std::chrono::seconds::zero() || max_age > max_max_age)
		/* limit to one day */
		max_age = max_max_age;

	auto item = NewFromPool<TranslateCacheItem>(pool_new_slice(*tcr.tcache->pool, "tcache_item",
								   tcr.tcache->slice_pool),
						    tcr.tcache->cache.SteadyNow(),
						    max_age);

	const AllocatorPtr alloc(item->GetPool());

	item->request.param =
		tcache_vary_copy(alloc, tcr.request.param,
				 response, TranslationCommand::PARAM);

	item->request.session =
		tcache_vary_copy(alloc, tcr.request.session,
				 response, TranslationCommand::SESSION);

	item->request.realm_session =
		tcache_vary_copy(alloc, tcr.request.realm_session,
				 response, TranslationCommand::REALM_SESSION);

	item->request.listener_tag =
		tcache_vary_copy(alloc, tcr.request.listener_tag,
				 response, TranslationCommand::LISTENER_TAG);

	item->request.local_address =
		!tcr.request.local_address.IsNull() &&
		(response.VaryContains(TranslationCommand::LOCAL_ADDRESS) ||
		 response.VaryContains(TranslationCommand::LOCAL_ADDRESS_STRING))
		? DupAddress(alloc, tcr.request.local_address)
		: nullptr;

	tcache_vary_copy(alloc, tcr.request.remote_host,
			 response, TranslationCommand::REMOTE_HOST);
	item->request.remote_host =
		tcache_vary_copy(alloc, tcr.request.remote_host,
				 response, TranslationCommand::REMOTE_HOST);
	item->request.host = tcache_vary_copy(alloc, tcr.request.host,
					      response, TranslationCommand::HOST);
	item->request.accept_language =
		tcache_vary_copy(alloc, tcr.request.accept_language,
				 response, TranslationCommand::LANGUAGE);
	item->request.user_agent =
		tcache_vary_copy(alloc, tcr.request.user_agent,
				 response, TranslationCommand::USER_AGENT);
	item->request.query_string =
		tcache_vary_copy(alloc, tcr.request.query_string,
				 response, TranslationCommand::QUERY_STRING);
	item->request.internal_redirect =
		tcache_vary_copy(alloc, tcr.request.internal_redirect,
				 response, TranslationCommand::INTERNAL_REDIRECT);
	item->request.enotdir =
		tcache_vary_copy(alloc, tcr.request.enotdir,
				 response, TranslationCommand::ENOTDIR_);
	item->request.user =
		tcache_vary_copy(alloc, tcr.request.user,
				 response, TranslationCommand::USER);

	const char *key;

	try {
		key = tcache_store_response(alloc, item->response, response,
					    tcr.request);
	} catch (...) {
		item->Destroy();
		throw;
	}

	assert(!item->response.easy_base ||
	       item->response.address.IsValidBase());

	if (key == nullptr)
		key = alloc.Dup(tcr.key);

	LogConcat(4, "TranslationCache", "store ", key);

	if (response.regex != nullptr) {
		try {
			item->regex = response.CompileRegex();
		} catch (...) {
			item->Destroy();
			throw;
		}
	} else {
		assert(!response.IsExpandable());
	}

	if (response.inverse_regex != nullptr) {
		try {
			item->inverse_regex = response.CompileInverseRegex();
		} catch (...) {
			item->Destroy();
			throw;
		}
	}

	item->stats = pool_stats(item->GetPool());

	if (response.VaryContains(TranslationCommand::HOST))
		tcr.tcache->per_host.insert(*item);

	if (response.site != nullptr)
		tcr.tcache->per_site.insert(*item);

	++tcr.tcache->stats.stores;
	TranslateCacheMatchContext match_ctx{tcr.request, tcr.find_base};
	tcr.tcache->cache.PutMatch(key, *item, tcache_item_match, &match_ctx);
	return item;
}

/**
 * Return a null-terminated string with the given URI minus the query
 * string.  This function is only needed because various functions
 * such as uri_path_verify_paranoid() require a null-terminated
 * string, but cannot cope with a query string.
 */
static const char *
UriWithoutQueryString(AllocatorPtr alloc, const char *uri) noexcept
{
	const char *qmark = strchr(uri, '?');
	if (qmark == nullptr)
		/* optimized fast path: return the original URI
		   pointer without allocations and without copying any
		   data */
		return uri;

	/* slow path: copy the string so the caller gets a
	   null-terminated string */
	return alloc.DupZ({uri, qmark});
}

/*
 * translate callback
 *
 */

void
TranslateCacheRequest::OnTranslateResponse(UniquePoolPtr<TranslateResponse> _response) noexcept
try {
	tcache->active = true;

	auto &response = *_response;

	if (!response.invalidate.empty())
		tcache->Invalidate(request,
				   response.invalidate,
				   nullptr);

	if (!cacheable) {
		if (key != nullptr)
			LogConcat(4, "TranslationCache", "ignore ", key);
	} else if (tcache_response_evaluate(response)) {
		tcache_store(*this, response);
	} else {
		LogConcat(4, "TranslationCache", "nocache ", key);
	}

	if (request.uri != nullptr && response.IsExpandable()) {
		const char *uri = UriWithoutQueryString(alloc, request.uri);
		tcache_expand_response(alloc, response,
				       response.CompileRegex(),
				       uri, request.host,
				       request.user);
	} else if (response.easy_base) {
		/* create a writable copy and apply the BASE */
		const char *uri = UriWithoutQueryString(alloc, request.uri);
		response.CacheLoad(alloc, response, uri);
	} else if (response.base != nullptr) {
		const char *uri = request.uri;
		const char *tail = require_base_tail(uri, response.base);
		tail = UriWithoutQueryString(alloc, tail);
		if (!response.unsafe_base && !uri_path_verify_paranoid(tail))
			throw HttpMessageResponse(HttpStatus::BAD_REQUEST,
						  "Malformed URI");
	}

	handler->OnTranslateResponse(std::move(_response));
} catch (...) {
	_response.reset();

	handler->OnTranslateError(std::current_exception());
}

void
TranslateCacheRequest::OnTranslateError(std::exception_ptr ep) noexcept
{
	LogConcat(4, "TranslationCache", "error ", key);

	handler->OnTranslateError(ep);
}

static void
tcache_hit(AllocatorPtr alloc,
	   const char *uri, const char *host, const char *user,
	   const char *key,
	   const TranslateCacheItem &item,
	   TranslateHandler &handler) noexcept
{
	auto response = UniquePoolPtr<TranslateResponse>::Make(alloc.GetPool());

	LogConcat(4, "TranslationCache", "hit ", key);

	try {
		response->CacheLoad(alloc, item.response, uri);
	} catch (...) {
		handler.OnTranslateError(std::current_exception());
		return;
	}

	if (uri != nullptr && response->IsExpandable()) {
		try {
			tcache_expand_response(alloc, *response, item.regex,
					       uri, host, user);
		} catch (...) {
			handler.OnTranslateError(std::current_exception());
			return;
		}
	}

	handler.OnTranslateResponse(std::move(response));
}

static void
tcache_miss(AllocatorPtr alloc, struct tcache &tcache,
	    const TranslateRequest &request, const char *key,
	    bool cacheable,
	    const StopwatchPtr &parent_stopwatch,
	    TranslateHandler &handler,
	    CancellablePointer &cancel_ptr) noexcept
{
	auto tcr = alloc.New<TranslateCacheRequest>(alloc, tcache,
						    request, key,
						    cacheable,
						    handler);

	if (cacheable)
		LogConcat(4, "TranslationCache", "miss ", key);

	tcache.next.SendRequest(alloc, request, parent_stopwatch,
				*tcr, cancel_ptr);
}

[[gnu::pure]]
static bool
tcache_validate_mtime(const TranslateResponse &response,
		      const char *key) noexcept
{
	if (response.validate_mtime.path == nullptr)
		return true;

	LogConcat(6, "TranslationCache", "[", key,
		  "] validate_mtime ", response.validate_mtime.mtime,
		  " ", response.validate_mtime.path);

	struct statx stx;
	if (statx(-1, response.validate_mtime.path,
		  AT_SYMLINK_NOFOLLOW|AT_STATX_DONT_SYNC,
		  STATX_TYPE|STATX_MTIME,
		  &stx) < 0) {
		if (errno == ENOENT && response.validate_mtime.mtime == 0) {
			/* the special value 0 matches when the file does not
			   exist */
			LogConcat(6, "TranslationCache", "[", key,
				  "] validate_mtime enoent ",
				  response.validate_mtime.path);
			return true;
		}

		LogConcat(3, "TranslationCache", "[", key,
			  "] failed to stat '", response.validate_mtime.path,
			  "': ", strerror(errno));
		return false;
	}

	if (!S_ISREG(stx.stx_mode)) {
		LogConcat(3, "TranslationCache", "[", key,
			  "] not a regular file: ", response.validate_mtime.path);
		return false;
	}

	if ((uint_least64_t)stx.stx_mtime.tv_sec == response.validate_mtime.mtime) {
		LogConcat(6, "TranslationCache", "[", key,
			  "] validate_mtime unmodified ",
			  response.validate_mtime.path);
		return true;
	} else {
		LogConcat(5, "TranslationCache", "[", key,
			  "] validate_mtime modified ",
			  response.validate_mtime.path);
		return false;
	}
}


/*
 * cache class
 *
 */

bool
TranslateCacheItem::Validate() const noexcept
{
	return tcache_validate_mtime(response, GetKey());
}

void
TranslateCacheItem::Destroy() noexcept
{
	pool_trash(pool);
	this->~TranslateCacheItem();
}

/*
 * constructor
 *
 */

inline
tcache::tcache(struct pool &_pool, EventLoop &event_loop,
	       TranslationService &_next, unsigned max_size,
	       bool handshake_cacheable)
	:pool(pool_new_dummy(&_pool, "translate_cache")),
	 slice_pool(4096, 32768, "translate_cache"),
	 cache(event_loop, max_size, this),
	 next(_next), active(handshake_cacheable)
{
	assert(max_size > 0);
}

TranslationCache::TranslationCache(struct pool &pool, EventLoop &event_loop,
				   TranslationService &next,
				   unsigned max_size,
				   bool handshake_cacheable)
	:cache(new tcache(pool, event_loop, next, max_size,
			  handshake_cacheable))
{
}

TranslationCache::~TranslationCache() noexcept = default;

void
TranslationCache::ForkCow(bool inherit) noexcept
{
	cache->slice_pool.ForkCow(inherit);
}

CacheStats
TranslationCache::GetStats() const noexcept
{
	return cache->stats;
}

void
TranslationCache::Flush() noexcept
{
	cache->cache.Flush();
	cache->slice_pool.Compress();
}


/*
 * methods
 *
 */

void
TranslationCache::SendRequest(AllocatorPtr alloc,
			      const TranslateRequest &request,
			      const StopwatchPtr &parent_stopwatch,
			      TranslateHandler &handler,
			      CancellablePointer &cancel_ptr) noexcept
{
	const bool cacheable = cache->active && tcache_request_evaluate(request);
	const char *key = tcache_request_key(alloc, request);
	TranslateCacheItem *item = cacheable
		? tcache_lookup(alloc, *cache, request, key)
		: nullptr;
	if (item != nullptr) {
		++cache->stats.hits;
		tcache_hit(alloc, request.uri, request.host, request.user, key,
			   *item, handler);
	} else {
		++cache->stats.misses;
		tcache_miss(alloc, *cache, request, key, cacheable,
			    parent_stopwatch,
			    handler, cancel_ptr);
	}
}
