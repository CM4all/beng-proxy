// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <max.kellermann@ionos.com>

#include "tconstruct.hxx"
#include "tprint.hxx"
#include "RecordingTranslateHandler.hxx"
#include "translation/Cache.hxx"
#include "translation/Stock.hxx"
#include "translation/Handler.hxx"
#include "translation/Request.hxx"
#include "translation/Response.hxx"
#include "translation/Transformation.hxx"
#include "translation/Protocol.hxx"
#include "widget/View.hxx"
#include "http/Status.hxx"
#include "http/Address.hxx"
#include "file/Address.hxx"
#include "cgi/Address.hxx"
#include "spawn/Mount.hxx"
#include "spawn/NamespaceOptions.hxx"
#include "pool/pool.hxx"
#include "PInstance.hxx"
#include "util/Cancellable.hxx"
#include "util/StringAPI.hxx"
#include "stopwatch.hxx"

#include <gtest/gtest.h>

using std::string_view_literals::operator""sv;

class MyTranslationService final : public TranslationService {
public:
	/* virtual methods from class TranslationService */
	void SendRequest(AllocatorPtr alloc,
			 const TranslateRequest &request,
			 const StopwatchPtr &parent_stopwatch,
			 TranslateHandler &handler,
			 CancellablePointer &cancel_ptr) noexcept override;
};

struct Instance : PInstance {
	MyTranslationService ts;
	TranslationCache cache;

	Instance()
		:cache(root_pool, event_loop, ts, 1024) {}
};

const TranslateResponse *next_response;

void
MyTranslationService::SendRequest(AllocatorPtr alloc,
				  const TranslateRequest &,
				  const StopwatchPtr &,
				  TranslateHandler &handler,
				  CancellablePointer &) noexcept
{
	if (next_response != nullptr) {
		auto response = UniquePoolPtr<TranslateResponse>::Make(alloc.GetPool());
		response->FullCopyFrom(alloc, *next_response);
		handler.OnTranslateResponse(std::move(response));
	} else
		handler.OnTranslateError(std::make_exception_ptr(std::runtime_error("Error")));
}

[[gnu::pure]]
static bool
StringEquals(const char *a, const char *b) noexcept
{
	if (a == nullptr || b == nullptr)
		return a == nullptr && b == nullptr;

	return StringIsEqual(a, b);
}

template<typename T>
[[gnu::pure]]
static bool
RawEquals(std::span<const T> a, std::span<const T> b) noexcept
{
	if (a.data() == nullptr || b.data() == nullptr)
		return (a.data() == nullptr) == (b.data() == nullptr);

	return a.size() == b.size() && memcmp(a.data(), b.data(), a.size_bytes()) == 0;
}

static bool
operator==(const Mount &a, const Mount &b) noexcept
{
	return StringEquals(a.source, b.source) &&
		StringIsEqual(a.target, b.target) &&
		a.type == b.type &&
		a.expand_source == b.expand_source &&
		a.writable == b.writable;
}

template<typename T>
static bool
AllEquals(const T &a, const T &b) noexcept
{
	using std::begin;
	using std::end;

	auto i = begin(a);
	for (const auto &j : b) {
		if (i == end(a))
			return false;

		if (!(*i == j))
			return false;

		++i;
	}

	return i == end(a);
}

template<typename T>
static bool
AllEquals(const std::shared_ptr<T> &a, const std::shared_ptr<T> &b) noexcept
{
	if (!a || !b)
		return (!a) == (!b);

	return AllEquals(*a, *b);
}

static bool
operator==(const MountNamespaceOptions &a,
	   const MountNamespaceOptions &b) noexcept
{
	return AllEquals(a.mounts, b.mounts);
}

static bool
operator==(const NamespaceOptions &a, const NamespaceOptions &b) noexcept
{
	return a.mount == b.mount;
}

static bool
operator==(const ChildOptions &a, const ChildOptions &b) noexcept
{
	return a.ns == b.ns;
}

static bool
operator==(const HttpAddress &a, const HttpAddress &b) noexcept
{
	return StringEquals(a.host_and_port, b.host_and_port) &&
		StringEquals(a.path, b.path);
}

static bool
operator==(const ResourceAddress &a, const ResourceAddress &b) noexcept
{
	if (a.type != b.type)
		return false;

	switch (a.type) {
	case ResourceAddress::Type::NONE:
		return true;

	case ResourceAddress::Type::LOCAL:
		EXPECT_NE(a.GetFile().path, nullptr);
		EXPECT_NE(b.GetFile().path, nullptr);

		return StringEquals(a.GetFile().path, b.GetFile().path) &&
			StringEquals(a.GetFile().gzipped, b.GetFile().gzipped) &&
			StringEquals(a.GetFile().base, b.GetFile().base) &&
			StringEquals(a.GetFile().content_type, b.GetFile().content_type);

	case ResourceAddress::Type::CGI:
		EXPECT_NE(a.GetCgi().path, nullptr);
		EXPECT_NE(b.GetCgi().path, nullptr);

		return a.GetCgi().options == b.GetCgi().options &&
			StringEquals(a.GetCgi().path, b.GetCgi().path) &&
			StringEquals(a.GetCgi().interpreter, b.GetCgi().interpreter) &&
			StringEquals(a.GetCgi().action, b.GetCgi().action) &&
			StringEquals(a.GetCgi().uri, b.GetCgi().uri) &&
			StringEquals(a.GetCgi().script_name, b.GetCgi().script_name) &&
			StringEquals(a.GetCgi().path_info, b.GetCgi().path_info) &&
			StringEquals(a.GetCgi().query_string, b.GetCgi().query_string) &&
			StringEquals(a.GetCgi().document_root, b.GetCgi().document_root);

	case ResourceAddress::Type::HTTP:
		return a.GetHttp() == b.GetHttp();

	default:
		/* not implemented */
		EXPECT_TRUE(false);
		return false;
	}
}

static bool
operator==(const Transformation &a, const Transformation &b) noexcept
{
	if (a.type != b.type)
		return false;

	switch (a.type) {
	case Transformation::Type::PROCESS:
		return a.u.processor.options == b.u.processor.options;

	case Transformation::Type::PROCESS_CSS:
		return a.u.css_processor.options == b.u.css_processor.options;

	case Transformation::Type::PROCESS_TEXT:
		return true;

	case Transformation::Type::FILTER:
		return a.u.filter.address == b.u.filter.address;
	}

	/* unreachable */
	EXPECT_TRUE(false);
	return false;
}

static bool
operator==(const WidgetView &a, const WidgetView &b) noexcept
{
	return StringEquals(a.name, b.name) &&
		a.address == b.address &&
		a.filter_4xx == b.filter_4xx &&
		AllEquals(a.transformations, b.transformations);
}

[[gnu::pure]]
static bool
AllEquals(const WidgetViewList &a, const WidgetViewList &b) noexcept
{
	return std::equal(a.begin(), a.end(), b.begin(), b.end());
}

static bool
operator==(const TranslationLayoutItem &a, const TranslationLayoutItem &b) noexcept
{
	return a.GetType() == b.GetType() && a.value == b.value;
}

static bool
operator==(const TranslateResponse &a, const TranslateResponse &b) noexcept
{
	return StringEquals(a.base, b.base) &&
		RawEquals(a.layout, b.layout) &&
		AllEquals(a.layout_items, b.layout_items) &&
		a.regex_tail == b.regex_tail &&
		StringEquals(a.regex, b.regex) &&
		StringEquals(a.inverse_regex, b.inverse_regex) &&
		a.easy_base == b.easy_base &&
		a.unsafe_base == b.unsafe_base &&
		StringEquals(a.uri, b.uri) &&
		StringEquals(a.redirect, b.redirect) &&
		StringEquals(a.test_path, b.test_path) &&
		RawEquals(a.check, b.check) &&
		RawEquals(a.want_full_uri, b.want_full_uri) &&
		a.address == b.address &&
		AllEquals(a.views, b.views);
}

static void
ExpectResponse(const RecordingTranslateHandler &handler,
	       const TranslateResponse &expected) noexcept
{
	EXPECT_TRUE(handler.finished);
	EXPECT_TRUE(!handler.error);
	ASSERT_TRUE(handler.response);
	EXPECT_EQ(*handler.response, expected);
}

static void
ExpectError(const RecordingTranslateHandler &handler) noexcept
{
	EXPECT_TRUE(handler.finished);
	EXPECT_FALSE(handler.response);
	EXPECT_TRUE(!!handler.error);
}

static void
Feed(struct pool &parent_pool, TranslationService &service,
     const TranslateRequest &request,
     const TranslateResponse &response) noexcept
{
	RecordingTranslateHandler handler(parent_pool);
	CancellablePointer cancel_ptr;

	next_response = &response;
	service.SendRequest(AllocatorPtr{handler.pool}, request, nullptr,
			    handler, cancel_ptr);

	ExpectResponse(handler, response);

}

static void
Feed(struct pool &parent_pool, TranslationService &service,
     const TranslateRequest &request,
     const TranslateResponse &feed_response,
     const TranslateResponse &expected_response) noexcept
{
	RecordingTranslateHandler handler(parent_pool);
	CancellablePointer cancel_ptr;

	next_response = &feed_response;
	service.SendRequest(AllocatorPtr{handler.pool}, request, nullptr,
			    handler, cancel_ptr);

	ExpectResponse(handler, expected_response);

}

static void
FeedError(struct pool &parent_pool, TranslationService &service,
	  const TranslateRequest &request,
	  const TranslateResponse &response) noexcept
{
	RecordingTranslateHandler handler(parent_pool);
	CancellablePointer cancel_ptr;

	next_response = &response;
	service.SendRequest(AllocatorPtr{handler.pool}, request, nullptr,
			    handler, cancel_ptr);

	ExpectError(handler);

}

static void
Cached(struct pool &parent_pool, TranslationService &service,
       const TranslateRequest &request,
       const TranslateResponse &response) noexcept
{
	RecordingTranslateHandler handler(parent_pool);
	CancellablePointer cancel_ptr;

	next_response = nullptr;
	service.SendRequest(AllocatorPtr{handler.pool}, request, nullptr,
			    handler, cancel_ptr);

	ExpectResponse(handler, response);
}

static void
CachedError(struct pool &parent_pool, TranslationService &service,
	    const TranslateRequest &request) noexcept
{
	RecordingTranslateHandler handler(parent_pool);
	CancellablePointer cancel_ptr;

	next_response = nullptr;
	service.SendRequest(AllocatorPtr{handler.pool}, request, nullptr,
			    handler, cancel_ptr);

	ExpectError(handler);

}

TEST(TranslationCache, Basic)
{
	Instance instance;
	struct pool &pool = instance.root_pool;
	auto &cache = instance.cache;

	const auto response1 = MakeResponse(pool).File("/var/www/index.html");
	Feed(pool, cache, MakeRequest("/"), response1);
	Cached(pool, cache, MakeRequest("/"), response1);

	Feed(pool, cache, MakeRequest("/foo/bar.html"),
	     MakeResponse(pool).Base("/foo/")
	     .File("bar.html", "/srv/foo/"));

	Cached(pool, cache, MakeRequest("/foo/index.html"),
	       MakeResponse(pool).Base("/foo/")
	       .File("index.html", "/srv/foo/"));

	Cached(pool, cache, MakeRequest("/foo/"),
	       MakeResponse(pool).Base("/foo/")
	       .File(".", "/srv/foo/"));

	// this URI path will be normalized
	Cached(pool, cache, MakeRequest("/foo//./index.html"),
	       MakeResponse(pool).Base("/foo/")
	       .File("index.html", "/srv/foo/"));

	CachedError(pool, cache, MakeRequest("/foo"));

	Cached(pool, cache, MakeRequest("/foo//bar"),
	       MakeResponse(pool).Base("/foo/")
	       .File("bar", "/srv/foo/"));

	Feed(pool, cache, MakeRequest("/cgi1/foo"),
	     MakeResponse(pool).Base("/cgi1/")
	     .Cgi("/usr/lib/cgi-bin/cgi.pl", "/cgi1/foo", "x/foo"));

	Cached(pool, cache, MakeRequest("/cgi1/a/b/c"),
	       MakeResponse(pool).Base("/cgi1/")
	       .Cgi("/usr/lib/cgi-bin/cgi.pl", "/cgi1/a/b/c", "x/a/b/c"));

	Feed(pool, cache, MakeRequest("/cgi2/foo"),
	     MakeResponse(pool).Base("/cgi2/")
	     .Cgi("/usr/lib/cgi-bin/cgi.pl", "/cgi2/foo", "foo"));

	Cached(pool, cache, MakeRequest("/cgi2/a/b/c"),
	       MakeResponse(pool).Base("/cgi2/")
	       .Cgi("/usr/lib/cgi-bin/cgi.pl", "/cgi2/a/b/c", "a/b/c"));
}

/**
 * Feed the cache with a request to the BASE.  This was buggy until
 * 4.0.30.
 */
TEST(TranslationCache, BaseRoot)
{
	Instance instance;
	struct pool &pool = instance.root_pool;
	auto &cache = instance.cache;

	Feed(pool, cache, MakeRequest("/base_root/"),
	     MakeResponse(pool).Base("/base_root/")
	     .File(".", "/var/www/"));

	Cached(pool, cache, MakeRequest("/base_root/hansi"),
	       MakeResponse(pool).Base("/base_root/")
	       .File("hansi", "/var/www/"));
}

TEST(TranslationCache, BaseMismatch)
{
	Instance instance;
	struct pool &pool = instance.root_pool;
	auto &cache = instance.cache;

	FeedError(pool, cache, MakeRequest("/base_mismatch/hansi"),
		  MakeResponse(pool).Base("/different_base/").File("/var/www/"));
}

/**
 * Test BASE+URI.
 */
TEST(TranslationCache, BaseUri)
{
	Instance instance;
	struct pool &pool = instance.root_pool;
	auto &cache = instance.cache;

	Feed(pool, cache, MakeRequest("/base_uri/foo"),
	     MakeResponse(pool).Base("/base_uri/")
	     .File("foo", "/var/www/")
	     .Uri("/modified/foo"));

	Cached(pool, cache, MakeRequest("/base_uri/hansi"),
	       MakeResponse(pool).Base("/base_uri/")
	       .File("hansi", "/var/www/")
	       .Uri("/modified/hansi"));
}

/**
 * Test BASE+REDIRECT.
 */
TEST(TranslationCache, BaseRedirect)
{
	Instance instance;
	struct pool &pool = instance.root_pool;
	auto &cache = instance.cache;

	Feed(pool, cache, MakeRequest("/base_redirect/foo"),
	     MakeResponse(pool).Base("/base_redirect/")
	     .File("foo", "/var/www/")
	     .Redirect("http://modified/foo"));

	Cached(pool, cache, MakeRequest("/base_redirect/hansi"),
	       MakeResponse(pool).Base("/base_redirect/")
	       .File("hansi", "/var/www/")
	       .Redirect("http://modified/hansi"));
}

/**
 * Test BASE+TEST_PATH.
 */
TEST(TranslationCache, BaseTestPath)
{
	Instance instance;
	struct pool &pool = instance.root_pool;
	auto &cache = instance.cache;

	Feed(pool, cache, MakeRequest("/base_test_path/foo"),
	     MakeResponse(pool).Base("/base_test_path/")
	     .File("foo", "/var/www/")
	     .TestPath("/modified/foo"));

	Cached(pool, cache, MakeRequest("/base_test_path/hansi"),
	       MakeResponse(pool).Base("/base_test_path/")
	       .File("hansi", "/var/www/")
	       .TestPath("/modified/hansi"));
}

TEST(TranslationCache, EasyBase)
{
	Instance instance;
	struct pool &pool = instance.root_pool;
	auto &cache = instance.cache;

	const auto request1 = MakeRequest("/easy/bar.html");

	const auto response1 = MakeResponse(pool).EasyBase("/easy/")
		.File(".", "/var/www/");
	const auto response1b = MakeResponse(pool).EasyBase("/easy/")
		.File("bar.html", "/var/www/");

	Feed(pool, cache, request1, response1, response1b);
	Cached(pool, cache, request1, response1b);

	Cached(pool, cache, MakeRequest("/easy/index.html"),
	       MakeResponse(pool).EasyBase("/easy/")
	       .File("index.html", "/var/www/"));
}

/**
 * Test EASY_BASE+URI.
 */
TEST(TranslationCache, EasyBaseUri)
{
	Instance instance;
	struct pool &pool = instance.root_pool;
	auto &cache = instance.cache;

	Feed(pool, cache, MakeRequest("/easy_base_uri/foo"),
	     MakeResponse(pool).EasyBase("/easy_base_uri/")
	     .File(".", "/var/www/")
	     .Uri("/modified/"),
	     MakeResponse(pool).EasyBase("/easy_base_uri/")
	     .File("foo", "/var/www/")
	     .Uri("/modified/foo"));

	Cached(pool, cache, MakeRequest("/easy_base_uri/hansi"),
	       MakeResponse(pool).EasyBase("/easy_base_uri/")
	       .File("hansi", "/var/www/")
	       .Uri("/modified/hansi"));
}

/**
 * Test EASY_BASE+URI where URIm needs normalization.
 */
TEST(TranslationCache, EasyBaseUriNormalize)
{
	Instance instance;
	struct pool &pool = instance.root_pool;
	auto &cache = instance.cache;

	Feed(pool, cache, MakeRequest("/easy_base_uri/foo//./bar"),
	     MakeResponse(pool).EasyBase("/easy_base_uri/")
	     .File(".", "/var/www/")
	     .Uri("/modified/"),
	     MakeResponse(pool).EasyBase("/easy_base_uri/")
	     .File("foo/bar", "/var/www/")
	     .Uri("/modified/foo//./bar"));

	Cached(pool, cache, MakeRequest("/easy_base_uri/hansi"),
	       MakeResponse(pool).EasyBase("/easy_base_uri/")
	       .File("hansi", "/var/www/")
	       .Uri("/modified/hansi"));
}

/**
 * Test EASY_BASE + TEST_PATH.
 */
TEST(TranslationCache, EasyBaseTestPath)
{
	Instance instance;
	struct pool &pool = instance.root_pool;
	auto &cache = instance.cache;

	Feed(pool, cache, MakeRequest("/easy_base_test_path/foo"),
	     MakeResponse(pool).EasyBase("/easy_base_test_path/")
	     .File(".", "/var/www/")
	     .TestPath("/modified/"),
	     MakeResponse(pool).EasyBase("/easy_base_test_path/")
	     .File("foo", "/var/www/")
	     .TestPath("/modified/foo"));

	Cached(pool, cache, MakeRequest("/easy_base_test_path/hansi"),
	       MakeResponse(pool).EasyBase("/easy_base_test_path/")
	       .File("hansi", "/var/www/")
	       .TestPath("/modified/hansi"));
}

TEST(TranslationCache, VaryInvalidate)
{
	Instance instance;
	struct pool &pool = instance.root_pool;
	auto &cache = instance.cache;

	static const TranslationCommand response5_vary[] = {
		TranslationCommand::QUERY_STRING,
	};

	static const TranslationCommand response5_invalidate[] = {
		TranslationCommand::QUERY_STRING,
	};

	const auto response5c = MakeResponse(pool).File("/srv/qs3")
		.Vary(response5_vary).Invalidate(response5_invalidate);

	const auto request6 = MakeRequest("/qs").QueryString("abc");
	const auto response5a = MakeResponse(pool).File("/srv/qs1")
		.Vary(response5_vary);

	Feed(pool, cache, request6, response5a);

	const auto request7 = MakeRequest("/qs").QueryString("xyz");
	const auto response5b = MakeResponse(pool).File("/srv/qs2")
		.Vary(response5_vary);
	Feed(pool, cache, request7, response5b);

	Cached(pool, cache, request6, response5a);
	Cached(pool, cache, request7, response5b);

	const auto request8 = MakeRequest("/qs/").QueryString("xyz");
	Feed(pool, cache, request8, response5c);

	Cached(pool, cache, request6, response5a);

	Feed(pool, cache, request7, response5c);
	Feed(pool, cache, request8, response5c);
	Feed(pool, cache, request7, response5c);
}

TEST(TranslationCache, InvalidateUri)
{
	Instance instance;
	struct pool &pool = instance.root_pool;
	auto &cache = instance.cache;

	/* feed the cache */

	const auto request1 = MakeRequest("/invalidate/uri");
	const auto response1 = MakeResponse(pool).File("/var/www/invalidate/uri");
	Feed(pool, cache, request1, response1);

	const auto request2 = MakeRequest("/invalidate/uri").Check("x");
	const auto response2 = MakeResponse(pool).File("/var/www/invalidate/uri");
	Feed(pool, cache, request2, response2);

	const auto request3 = MakeRequest("/invalidate/uri")
		.Status(HttpStatus::INTERNAL_SERVER_ERROR);
	const auto response3 = MakeResponse(pool).File("/var/www/500/invalidate/uri");
	Feed(pool, cache, request3, response3);

	const auto request4 = MakeRequest("/invalidate/uri")
		.Status(HttpStatus::INTERNAL_SERVER_ERROR)
		.Check("x");
	const auto response4 = MakeResponse(pool).File("/var/www/500/check/invalidate/uri");
	Feed(pool, cache, request4, response4);

	const auto request4b = MakeRequest("/invalidate/uri")
		.Status(HttpStatus::INTERNAL_SERVER_ERROR)
		.Check("x")
		.WantFullUri("a\0/b"sv);
	const auto response4b = MakeResponse(pool).File("/var/www/500/check/wfu/invalidate/uri");
	Feed(pool, cache, request4b, response4b);

	/* verify the cache items */

	Cached(pool, cache, request1, response1);
	Cached(pool, cache, request2, response2);
	Cached(pool, cache, request3, response3);
	Cached(pool, cache, request4, response4);
	Cached(pool, cache, request4b, response4b);

	/* invalidate all cache items */

	static const TranslationCommand response5_invalidate[] = {
		TranslationCommand::URI,
	};

	Feed(pool, cache,
	     MakeRequest("/invalidate/uri").Status(HttpStatus::NOT_FOUND),
	     MakeResponse(pool).File("/var/www/404/invalidate/uri")
	     .Invalidate(response5_invalidate));

	/* check if all cache items have really been deleted */

	CachedError(pool, cache, request1);
	CachedError(pool, cache, request2);
	CachedError(pool, cache, request3);
	CachedError(pool, cache, request4);
	CachedError(pool, cache, request4b);
}

TEST(TranslationCache, Regex)
{
	Instance instance;
	struct pool &pool = instance.root_pool;
	auto &cache = instance.cache;

	/* add the "inverse_regex" test to the cache first */
	const auto request_i1 = MakeRequest("/regex/foo");
	const auto response_i1 = MakeResponse(pool)
		.File("foo", "/var/www/regex/other/")
		.Base("/regex/").InverseRegex("\\.(jpg|html)$");
	Feed(pool, cache, request_i1, response_i1);

	/* fill the cache */
	const auto request1 = MakeRequest("/regex/a/foo.jpg");
	const auto response1 = MakeResponse(pool)
		.File("a/foo.jpg", "/var/www/regex/images/")
		.Base("/regex/").Regex("\\.jpg$");
	Feed(pool, cache, request1, response1);

	/* regex mismatch */
	const auto request2 = MakeRequest("/regex/b/foo.html");
	const auto response2 = MakeResponse(pool)
		.File("b/foo.html", "/var/www/regex/html/")
		.Base("/regex/").Regex("\\.html$");
	Feed(pool, cache, request2, response2);

	/* regex match */
	const auto request3 = MakeRequest("/regex/c/bar.jpg");
	const auto response3 = MakeResponse(pool)
		.File("c/bar.jpg", "/var/www/regex/images/")
		.Base("/regex/").Regex("\\.jpg$");
	Cached(pool, cache, request3, response3);

	/* second regex match */
	const auto request4 = MakeRequest("/regex/d/bar.html");
	const auto response4 = MakeResponse(pool)
		.File("d/bar.html", "/var/www/regex/html/")
		.Base("/regex/").Regex("\\.html$");
	Cached(pool, cache, request4, response4);

	/* see if the "inverse_regex" cache item is still there */
	const auto request_i2 = MakeRequest("/regex/bar");
	const auto response_i2 = MakeResponse(pool)
		.File("bar", "/var/www/regex/other/")
		.Base("/regex/").InverseRegex("\\.(jpg|html)$");
	Cached(pool, cache, request_i2, response_i2);
}

TEST(TranslationCache, RegexError)
{
	Instance instance;
	struct pool &pool = instance.root_pool;
	auto &cache = instance.cache;

	const auto request = MakeRequest("/regex-error");
	const auto response = MakeResponse(pool).File("/error")
		.Base("/regex/").Regex("(");

	/* this must fail */
	FeedError(pool, cache, request, response);
}

TEST(TranslationCache, RegexTail)
{
	Instance instance;
	struct pool &pool = instance.root_pool;
	auto &cache = instance.cache;

	Feed(pool, cache, MakeRequest("/regex_tail/a/foo.jpg"),
	     MakeResponse(pool)
		.File("a/foo.jpg", "/var/www/regex/images/")
	     .Base("/regex_tail/").RegexTail("^a/"));

	CachedError(pool, cache, MakeRequest("/regex_tail/b/foo.html"));

	Cached(pool, cache, MakeRequest("/regex_tail/a/bar.jpg"),
	       MakeResponse(pool)
	       .File("a/bar.jpg", "/var/www/regex/images/")
	       .Base("/regex_tail/").RegexTail("^a/"));

	CachedError(pool, cache, MakeRequest("/regex_tail/%61/escaped.html"));
}

TEST(TranslationCache, RegexTailUnescape)
{
	Instance instance;
	struct pool &pool = instance.root_pool;
	auto &cache = instance.cache;

	Feed(pool, cache, MakeRequest("/regex_unescape/a/foo.jpg"),
	     MakeResponse(pool)
	     .File("a/foo.jpg", "/var/www/regex/images/")
	     .Base("/regex_unescape/").RegexTailUnescape("^a/"));

	CachedError(pool, cache, MakeRequest("/regex_unescape/b/foo.html"));

	Cached(pool, cache, MakeRequest("/regex_unescape/a/bar.jpg"),
	       MakeResponse(pool)
	       .File("a/bar.jpg", "/var/www/regex/images/")
	       .Base("/regex_unescape/").RegexTailUnescape("^a/"));

	Cached(pool, cache, MakeRequest("/regex_unescape/%61/escaped.html"),
	       MakeResponse(pool)
	       .File("a/escaped.html", "/var/www/regex/images/")
	       .Base("/regex_unescape/").RegexTailUnescape("^a/"));
}

TEST(TranslationCache, Expand)
{
	Instance instance;
	struct pool &pool = instance.root_pool;
	auto &cache = instance.cache;

	/* add to cache */

	Feed(pool, cache, MakeRequest("/regex-expand/b=c"),
	     MakeResponse(pool)
	     .Base("/regex-expand/").Regex("^/regex-expand/(.+=.+)$")
	     .Cgi(MakeCgiAddress(pool, "/usr/lib/cgi-bin/foo.cgi").ExpandPathInfo("/a/\\1")),
	     MakeResponse(pool)
	     .Base("/regex-expand/").Regex("^/regex-expand/(.+=.+)$")
	     .Cgi(MakeCgiAddress(pool, "/usr/lib/cgi-bin/foo.cgi", nullptr,
				 "/a/b=c")));

	/* check match */

	Cached(pool, cache, MakeRequest("/regex-expand/d=e"),
	       MakeResponse(pool)
	       .Base("/regex-expand/").Regex("^/regex-expand/(.+=.+)$")
	       .Cgi(MakeCgiAddress(pool, "/usr/lib/cgi-bin/foo.cgi", nullptr,
				   "/a/d=e")));
}

TEST(TranslationCache, ExpandLocal)
{
	Instance instance;
	struct pool &pool = instance.root_pool;
	auto &cache = instance.cache;

	/* add to cache */

	Feed(pool, cache, MakeRequest("/regex-expand2/foo/bar.jpg/b=c"),
	     MakeResponse(pool)
	     .Base("/regex-expand2/")
	     .Regex("^/regex-expand2/(.+\\.jpg)/([^/]+=[^/]+)$")
	     .File(MakeFileAddress("/dummy").ExpandPath("/var/www/\\1")),
	     MakeResponse(pool)
	     .Base("/regex-expand2/")
	     .Regex("^/regex-expand2/(.+\\.jpg)/([^/]+=[^/]+)$")
	     .File(MakeFileAddress("/var/www/foo/bar.jpg")));

	/* check match */

	Cached(pool, cache, MakeRequest("/regex-expand2/x/y/z.jpg/d=e"),
	       MakeResponse(pool)
	       .Base("/regex-expand2/")
	       .Regex("^/regex-expand2/(.+\\.jpg)/([^/]+=[^/]+)$")
	       .File("/var/www/x/y/z.jpg"));

	/* normalize */

	Cached(pool, cache, MakeRequest("/regex-expand2/a//b/./c.jpg/x=y"),
	       MakeResponse(pool)
	       .Base("/regex-expand2/")
	       .Regex("^/regex-expand2/(.+\\.jpg)/([^/]+=[^/]+)$")
	       .File("/var/www/a/b/c.jpg"));
}

TEST(TranslationCache, ExpandLocalFilter)
{
	Instance instance;
	struct pool &pool = instance.root_pool;
	auto &cache = instance.cache;

	/* add to cache */

	Feed(pool, cache, MakeRequest("/regex-expand3/foo/bar.jpg/b=c"),
	     MakeResponse(pool)
	     .Base("/regex-expand3/")
	     .Regex("^/regex-expand3/(.+\\.jpg)/([^/]+=[^/]+)$")
	     .Filter(MakeCgiAddress(pool, "/usr/lib/cgi-bin/image-processor.cgi").ExpandPathInfo("/\\2"))
	     .File(MakeFileAddress("/dummy").ExpandPath("/var/www/\\1")),
	     MakeResponse(pool)
	     .Base("/regex-expand3/")
	     .Regex("^/regex-expand3/(.+\\.jpg)/([^/]+=[^/]+)$")
	     .Filter(MakeCgiAddress(pool, "/usr/lib/cgi-bin/image-processor.cgi", nullptr,
				    "/b=c"))
	     .File(MakeFileAddress("/var/www/foo/bar.jpg")));

	/* check match */

	Cached(pool, cache, MakeRequest("/regex-expand3/x/y/z.jpg/d=e"),
	       MakeResponse(pool)
	       .Base("/regex-expand3/")
	       .Regex("^/regex-expand3/(.+\\.jpg)/([^/]+=[^/]+)$")
	       .Filter(MakeCgiAddress(pool, "/usr/lib/cgi-bin/image-processor.cgi", nullptr,
				      "/d=e"))
	       .File("/var/www/x/y/z.jpg"));
}

TEST(TranslationCache, ExpandUri)
{
	Instance instance;
	struct pool &pool = instance.root_pool;
	auto &cache = instance.cache;

	/* add to cache */

	Feed(pool, cache, MakeRequest("/regex-expand4/foo/bar.jpg/b=c"),
	     MakeResponse(pool)
	     .Base("/regex-expand4/")
	     .Regex("^/regex-expand4/(.+\\.jpg)/([^/]+=[^/]+)$")
	     .Http(MakeHttpAddress("/foo/bar.jpg").ExpandPath("/\\1")),
	     MakeResponse(pool)
	     .Base("/regex-expand4/")
	     .Regex("^/regex-expand4/(.+\\.jpg)/([^/]+=[^/]+)$")
	     .Http(MakeHttpAddress("/foo/bar.jpg")));

	/* check match */

	Cached(pool, cache, MakeRequest("/regex-expand4/x/y/z.jpg/d=e"),
	       MakeResponse(pool)
	       .Base("/regex-expand4/")
	       .Regex("^/regex-expand4/(.+\\.jpg)/([^/]+=[^/]+)$")
	       .Http(MakeHttpAddress("/x/y/z.jpg")));
}

TEST(TranslationCache, AutoBase)
{
	Instance instance;
	struct pool &pool = instance.root_pool;
	auto &cache = instance.cache;

	/* store response */

	Feed(pool, cache, MakeRequest("/auto-base/foo.cgi/bar"),
	     MakeResponse(pool)
	     .AutoBase()
	     .Cgi("/usr/lib/cgi-bin/foo.cgi", "/auto-base/foo.cgi/bar", "/bar"));

	/* check if BASE was auto-detected */

	Cached(pool, cache, MakeRequest("/auto-base/foo.cgi/check"),
	       MakeResponse(pool)
	       .AutoBase().Base("/auto-base/foo.cgi/")
	       .Cgi("/usr/lib/cgi-bin/foo.cgi", "/auto-base/foo.cgi/check", "/check"));
}

/**
 * Test CHECK + BASE.
 */
TEST(TranslationCache, BaseCheck)
{
	Instance instance;
	struct pool &pool = instance.root_pool;
	auto &cache = instance.cache;

	/* feed the cache */

	Feed(pool, cache, MakeRequest("/a/b/c.html"),
	     MakeResponse(pool).Base("/a/").Check("x"));

	Feed(pool, cache, MakeRequest("/a/b/c.html").Check("x"),
	     MakeResponse(pool).Base("/a/b/")
	     .File("c.html", "/var/www/vol0/a/b/"));

	Feed(pool, cache, MakeRequest("/a/d/e.html").Check("x"),
	     MakeResponse(pool).Base("/a/d/")
	     .File("e.html", "/var/www/vol1/a/d/"));

	/* now check whether the translate cache matches the BASE
	   correctly */

	const auto response4 = MakeResponse(pool).Base("/a/").Check("x");

	Cached(pool, cache, MakeRequest("/a/f/g.html"), response4);
	Cached(pool, cache, MakeRequest("/a/b/0/1.html"), response4);

	Cached(pool, cache, MakeRequest("/a/b/0/1.html").Check("x"),
	       MakeResponse(pool).Base("/a/b/")
	       .File("0/1.html", "/var/www/vol0/a/b/"));

	Cached(pool, cache, MakeRequest("/a/d/2/3.html").Check("x"),
	       MakeResponse(pool).Base("/a/d/")
	       .File("2/3.html", "/var/www/vol1/a/d/"));

	/* expect cache misses */

	CachedError(pool, cache, MakeRequest("/a/f/g.html").Check("y"));
}

/**
 * Test WANT_FULL_URI + BASE.
 */
TEST(TranslationCache, BaseWantFullUri)
{
	Instance instance;
	struct pool &pool = instance.root_pool;
	auto &cache = instance.cache;

	/* feed the cache */

	Feed(pool, cache, MakeRequest("/wfu/a/b/c.html"),
	     MakeResponse(pool).Base("/wfu/a/").WantFullUri("x"));

	Feed(pool, cache, MakeRequest("/wfu/a/b/c.html").WantFullUri("x"),
	     MakeResponse(pool).Base("/wfu/a/b/")
	     .File("c.html", "/var/www/vol0/a/b/"));

	Feed(pool, cache, MakeRequest("/wfu/a/d/e.html").WantFullUri("x"),
	     MakeResponse(pool).Base("/wfu/a/d/")
	     .File("e.html", "/var/www/vol1/a/d/"));

	/* now check whether the translate cache matches the BASE
	   correctly */

	const auto response4 = MakeResponse(pool).Base("/wfu/a/").WantFullUri("x");

	Cached(pool, cache, MakeRequest("/wfu/a/f/g.html"), response4);
	Cached(pool, cache, MakeRequest("/wfu/a/b/0/1.html"), response4);

	Cached(pool, cache, MakeRequest("/wfu/a/b/0/1.html").WantFullUri("x"),
	       MakeResponse(pool).Base("/wfu/a/b/")
	       .File("0/1.html", "/var/www/vol0/a/b/"));

	Cached(pool, cache, MakeRequest("/wfu/a/d/2/3.html").WantFullUri("x"),
	       MakeResponse(pool).Base("/wfu/a/d/")
	       .File("2/3.html", "/var/www/vol1/a/d/"));

	/* expect cache misses */

	CachedError(pool, cache,
		    MakeRequest("/wfu/a/f/g.html").WantFullUri("y"));
}

/**
 * Test UNSAFE_BASE.
 */
TEST(TranslationCache, UnsafeBase)
{
	Instance instance;
	struct pool &pool = instance.root_pool;
	auto &cache = instance.cache;

	/* feed */
	Feed(pool, cache, MakeRequest("/unsafe_base1/foo"),
	     MakeResponse(pool).Base("/unsafe_base1/")
	     .File("foo", "/var/www/"));

	Feed(pool, cache, MakeRequest("/unsafe_base2/foo"),
	     MakeResponse(pool).UnsafeBase("/unsafe_base2/")
	     .File("foo", "/var/www/"));

	/* fail (no UNSAFE_BASE) */

	CachedError(pool, cache, MakeRequest("/unsafe_base1/../x"));

	/* success (with UNSAFE_BASE) */

	Cached(pool, cache, MakeRequest("/unsafe_base2/../x"),
	       MakeResponse(pool).UnsafeBase("/unsafe_base2/")
	       .File("../x", "/var/www/"));
}

/**
 * Test UNSAFE_BASE + EXPAND_PATH.
 */
TEST(TranslationCache, ExpandUnsafeBase)
{
	Instance instance;
	struct pool &pool = instance.root_pool;
	auto &cache = instance.cache;

	/* feed */

	Feed(pool, cache, MakeRequest("/expand_unsafe_base1/foo"),
	     MakeResponse(pool).Base("/expand_unsafe_base1/")
	     .Regex("^/expand_unsafe_base1/(.*)$")
	     .File(MakeFileAddress("/var/www/foo.html").ExpandPath("/var/www/\\1.html")),
	     MakeResponse(pool).Base("/expand_unsafe_base1/")
	     .Regex("^/expand_unsafe_base1/(.*)$")
	     .File(MakeFileAddress("/var/www/foo.html")));

	Feed(pool, cache, MakeRequest("/expand_unsafe_base2/foo"),
	     MakeResponse(pool).UnsafeBase("/expand_unsafe_base2/")
	     .Regex("^/expand_unsafe_base2/(.*)$")
	     .File(MakeFileAddress("/var/www/foo.html").ExpandPath("/var/www/\\1.html")),
	     MakeResponse(pool).UnsafeBase("/expand_unsafe_base2/")
	     .Regex("^/expand_unsafe_base2/(.*)$")
	     .File(MakeFileAddress("/var/www/foo.html")));

	/* fail (no UNSAFE_BASE) */

	CachedError(pool, cache, MakeRequest("/expand_unsafe_base1/../x"));

	/* success (with UNSAFE_BASE) */

	Cached(pool, cache, MakeRequest("/expand_unsafe_base2/../x"),
	       MakeResponse(pool).UnsafeBase("/expand_unsafe_base2/")
	       .Regex("^/expand_unsafe_base2/(.*)$")
	       .File(MakeFileAddress("/var/www/../x.html")));
}

TEST(TranslationCache, Layout)
{
	Instance instance;
	struct pool &pool = instance.root_pool;
	auto &cache = instance.cache;

	Feed(pool, cache, MakeRequest("/foo/a/b"),
	     MakeResponse(pool).Base("/")
	     .Layout("_foo", {"/foo/", "/bar/"}));

	Cached(pool, cache, MakeRequest("/bar/"),
	       MakeResponse(pool).Base("/")
	       .Layout("_foo", {"/foo/", "/bar/"}));

	Cached(pool, cache, MakeRequest("/"),
	       MakeResponse(pool).Base("/")
	       .Layout("_foo", {"/foo/", "/bar/"}));

	CachedError(pool, cache, MakeRequest("/").Layout("_foo", nullptr));

	Feed(pool, cache, MakeRequest("/index").Layout("_foo", nullptr),
	     MakeResponse(pool).EasyBase("/").File(".", "/mnt/root/"),
	     MakeResponse(pool).EasyBase("/").File("index", "/mnt/root/"));
	Feed(pool, cache, MakeRequest("/bar/c/d").Layout("_foo", "/bar/"),
	     MakeResponse(pool).EasyBase("/bar/c/").File(".", "/mnt/bar/C/"),
	     MakeResponse(pool).EasyBase("/bar/c/").File("d", "/mnt/bar/C/"));
	Feed(pool, cache, MakeRequest("/bar/e/f").Layout("_foo", "/bar/"),
	     MakeResponse(pool).EasyBase("/bar/e/").File(".", "/mnt/bar/E/"),
	     MakeResponse(pool).EasyBase("/bar/e/").File("f", "/mnt/bar/E/"));
	Feed(pool, cache, MakeRequest("/foo/a/b").Layout("_foo", "/foo/"),
	     MakeResponse(pool).EasyBase("/foo/").File(".", "/mnt/foo/"),
	     MakeResponse(pool).EasyBase("/foo/").File("a/b", "/mnt/foo/"));

	Cached(pool, cache, MakeRequest("/x/y/z").Layout("_foo", nullptr),
	       MakeResponse(pool).EasyBase("/").File("x/y/z", "/mnt/root/"));
	Cached(pool, cache, MakeRequest("/bar/c/blubb").Layout("_foo", "/bar/"),
	       MakeResponse(pool).EasyBase("/bar/c/").File("blubb", "/mnt/bar/C/"));
	Cached(pool, cache, MakeRequest("/bar/e/blubb").Layout("_foo", "/bar/"),
	       MakeResponse(pool).EasyBase("/bar/e/").File("blubb", "/mnt/bar/E/"));
	Cached(pool, cache, MakeRequest("/foo/blubb").Layout("_foo", "/foo/"),
	       MakeResponse(pool).EasyBase("/foo/").File("blubb", "/mnt/foo/"));
}

TEST(TranslationCache, ExpandBindMount)
{
	Instance instance;
	struct pool &pool = instance.root_pool;
	auto &cache = instance.cache;

	/* add to cache */

	Feed(pool, cache, MakeRequest("/expand_bind_mount/foo"),
	     MakeResponse(pool).Base("/expand_bind_mount/")
	     .Regex("^/expand_bind_mount/(.+)$")
	     .Cgi(MakeCgiAddress(pool, "/usr/lib/cgi-bin/foo.cgi")
		  .BindMount("/home/\\1", "/mnt", true)
		  .BindMount("/etc", "/etc")),
	     MakeResponse(pool).Base("/expand_bind_mount/")
	     .Regex("^/expand_bind_mount/(.+)$")
	     .Cgi(MakeCgiAddress(pool, "/usr/lib/cgi-bin/foo.cgi")
		  .BindMount("/home/foo", "/mnt")
		  .BindMount("/etc", "/etc")));

	Cached(pool, cache, MakeRequest("/expand_bind_mount/bar"),
	       MakeResponse(pool).Base("/expand_bind_mount/")
	       .Regex("^/expand_bind_mount/(.+)$")
	       .Cgi(MakeCgiAddress(pool, "/usr/lib/cgi-bin/foo.cgi")
		    .BindMount("/home/bar", "/mnt")
		    .BindMount("/etc", "/etc")));
}
