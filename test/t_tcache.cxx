/*
 * Copyright 2007-2017 Content Management AG
 * All rights reserved.
 *
 * author: Max Kellermann <mk@cm4all.com>
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * - Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 *
 * - Redistributions in binary form must reproduce the above copyright
 * notice, this list of conditions and the following disclaimer in the
 * documentation and/or other materials provided with the
 * distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE
 * FOUNDATION OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED
 * OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "tconstruct.hxx"
#include "translation/Cache.hxx"
#include "translation/Stock.hxx"
#include "translation/Handler.hxx"
#include "translation/Request.hxx"
#include "translation/Response.hxx"
#include "translation/Transformation.hxx"
#include "translation/Protocol.hxx"
#include "widget/View.hxx"
#include "http_address.hxx"
#include "file_address.hxx"
#include "delegate/Address.hxx"
#include "cgi_address.hxx"
#include "spawn/MountList.hxx"
#include "spawn/NamespaceOptions.hxx"
#include "pool/pool.hxx"
#include "PInstance.hxx"
#include "util/Cancellable.hxx"

#include <gtest/gtest.h>

#include <string.h>

struct Instance : PInstance {
    struct tcache *cache;

    Instance()
        :cache(translate_cache_new(root_pool, event_loop,
                                   *(TranslateStock *)0x1, 1024)) {}

    ~Instance() {
        translate_cache_close(cache);
    }
};

const TranslateResponse *next_response, *expected_response;

void
tstock_translate(gcc_unused TranslateStock &stock, struct pool &pool,
                 gcc_unused const TranslateRequest &request,
                 const TranslateHandler &handler, void *ctx,
                 gcc_unused CancellablePointer &cancel_ptr)
{
    if (next_response != nullptr) {
        auto response = NewFromPool<MakeResponse>(pool, pool, *next_response);
        handler.response(*response, ctx);
    } else
        handler.error(std::make_exception_ptr(std::runtime_error("Error")),
                      ctx);
}

static bool
string_equals(const char *a, const char *b)
{
    if (a == nullptr || b == nullptr)
        return a == nullptr && b == nullptr;

    return strcmp(a, b) == 0;
}

template<typename T>
static bool
buffer_equals(ConstBuffer<T> a, ConstBuffer<T> b)
{
    if (a.IsNull() || b.IsNull())
        return a.IsNull() == b.IsNull();

    return a.size == b.size && memcmp(a.data, b.data, a.ToVoid().size) == 0;
}

static bool
Equals(const MountList &a, const MountList &b)
{
    return strcmp(a.source, b.source) == 0 &&
        strcmp(a.target, b.target) == 0 &&
        a.expand_source == b.expand_source;
}

static bool
Equals(const MountList *a, const MountList *b)
{
    for (; a != nullptr; a = a->next, b = b->next)
        if (b == nullptr || !Equals(*a, *b))
            return false;

    return b == nullptr;
}

static bool
Equals(const MountNamespaceOptions &a, const MountNamespaceOptions &b) noexcept
{
    return Equals(a.mounts, b.mounts);
}

static bool
Equals(const NamespaceOptions &a, const NamespaceOptions &b)
{
    return Equals(a.mount, b.mount);
}

static bool
Equals(const ChildOptions &a, const ChildOptions &b)
{
    return Equals(a.ns, b.ns);
}

static bool
Equals(const DelegateAddress &a, const DelegateAddress &b)
{
    return string_equals(a.delegate, b.delegate) &&
        Equals(a.child_options, b.child_options);
}

static bool
http_address_equals(const HttpAddress *a,
                    const HttpAddress *b)
{
    return string_equals(a->host_and_port, b->host_and_port) &&
        string_equals(a->path, b->path);
}

static bool
resource_address_equals(const ResourceAddress *a,
                        const ResourceAddress *b)
{
    EXPECT_NE(a, nullptr);
    EXPECT_NE(b, nullptr);

    if (a->type != b->type)
        return false;

    switch (a->type) {
    case ResourceAddress::Type::NONE:
        return true;

    case ResourceAddress::Type::LOCAL:
        EXPECT_NE(a->GetFile().path, nullptr);
        EXPECT_NE(b->GetFile().path, nullptr);

        return string_equals(a->GetFile().path, b->GetFile().path) &&
            string_equals(a->GetFile().deflated, b->GetFile().deflated) &&
            string_equals(a->GetFile().gzipped, b->GetFile().gzipped) &&
            string_equals(a->GetFile().content_type, b->GetFile().content_type) &&
            string_equals(a->GetFile().document_root, b->GetFile().document_root) &&
            (a->GetFile().delegate == nullptr) == (b->GetFile().delegate == nullptr) &&
            (a->GetFile().delegate == nullptr ||
             Equals(*a->GetFile().delegate, *b->GetFile().delegate));

    case ResourceAddress::Type::CGI:
        EXPECT_NE(a->GetCgi().path, nullptr);
        EXPECT_NE(b->GetCgi().path, nullptr);

        return Equals(a->GetCgi().options, b->GetCgi().options) &&
            string_equals(a->GetCgi().path, b->GetCgi().path) &&
            string_equals(a->GetCgi().interpreter, b->GetCgi().interpreter) &&
            string_equals(a->GetCgi().action, b->GetCgi().action) &&
            string_equals(a->GetCgi().uri, b->GetCgi().uri) &&
            string_equals(a->GetCgi().script_name, b->GetCgi().script_name) &&
            string_equals(a->GetCgi().path_info, b->GetCgi().path_info) &&
            string_equals(a->GetCgi().query_string, b->GetCgi().query_string) &&
            string_equals(a->GetCgi().document_root, b->GetCgi().document_root);

    case ResourceAddress::Type::HTTP:
        return http_address_equals(&a->GetHttp(), &b->GetHttp());

    default:
        /* not implemented */
        EXPECT_TRUE(false);
        return false;
    }
}

static bool
transformation_equals(const Transformation *a,
                      const Transformation *b)
{
    EXPECT_NE(a, nullptr);
    EXPECT_NE(b, nullptr);

    if (a->type != b->type)
        return false;

    switch (a->type) {
    case Transformation::Type::PROCESS:
        return a->u.processor.options == b->u.processor.options;

    case Transformation::Type::PROCESS_CSS:
        return a->u.css_processor.options == b->u.css_processor.options;

    case Transformation::Type::PROCESS_TEXT:
        return true;

    case Transformation::Type::FILTER:
        return resource_address_equals(&a->u.filter.address,
                                       &b->u.filter.address);

    case Transformation::Type::SUBST:
        return string_equals(a->u.subst.yaml_file, b->u.subst.yaml_file);
    }

    /* unreachable */
    EXPECT_TRUE(false);
    return false;
}

static bool
transformation_chain_equals(const Transformation *a,
                  const Transformation *b)
{
    while (a != nullptr && b != nullptr) {
        if (!transformation_equals(a, b))
            return false;

        a = a->next;
        b = b->next;
    }

    return a == nullptr && b == nullptr;
}

static bool
view_equals(const WidgetView *a, const WidgetView *b)
{
    EXPECT_NE(a, nullptr);
    EXPECT_NE(b, nullptr);

    return string_equals(a->name, b->name) &&
        resource_address_equals(&a->address, &b->address) &&
        a->filter_4xx == b->filter_4xx &&
        transformation_chain_equals(a->transformation, b->transformation);
}

static bool
view_chain_equals(const WidgetView *a, const WidgetView *b)
{
    while (a != nullptr && b != nullptr) {
        if (!view_equals(a, b))
            return false;

        a = a->next;
        b = b->next;
    }

    return a == nullptr && b == nullptr;
}

static bool
translate_response_equals(const TranslateResponse *a,
                          const TranslateResponse *b)
{
    if (a == nullptr || b == nullptr)
        return a == nullptr && b == nullptr;

    return string_equals(a->base, b->base) &&
        a->regex_tail == b->regex_tail &&
        string_equals(a->regex, b->regex) &&
        string_equals(a->inverse_regex, b->inverse_regex) &&
        a->easy_base == b->easy_base &&
        a->unsafe_base == b->unsafe_base &&
        string_equals(a->uri, b->uri) &&
        string_equals(a->redirect, b->redirect) &&
        string_equals(a->test_path, b->test_path) &&
        buffer_equals(a->check, b->check) &&
        buffer_equals(a->want_full_uri, b->want_full_uri) &&
        resource_address_equals(&a->address, &b->address) &&
        view_chain_equals(a->views, b->views);
}

static void
my_translate_response(TranslateResponse &response,
                      gcc_unused void *ctx)
{
    EXPECT_TRUE(translate_response_equals(&response, expected_response));
}

static void
my_translate_error(gcc_unused std::exception_ptr ep, gcc_unused void *ctx)
{
    EXPECT_EQ(expected_response, nullptr);
}

static constexpr TranslateHandler my_translate_handler = {
    .response = my_translate_response,
    .error = my_translate_error,
};

TEST(TranslationCache, Basic)
{
    Instance instance;
    struct pool *pool = instance.root_pool;
    auto *cache = instance.cache;

    CancellablePointer cancel_ptr;

    const auto request1 = MakeRequest("/");
    const auto response1 = MakeResponse().File("/var/www/index.html");
    next_response = expected_response = &response1;
    translate_cache(*pool, *cache, request1,
                    my_translate_handler, nullptr, cancel_ptr);

    next_response = nullptr;
    translate_cache(*pool, *cache, request1,
                    my_translate_handler, nullptr, cancel_ptr);

    const auto request2 = MakeRequest("/foo/bar.html");
    const auto response2 = MakeResponse().Base("/foo/").File("/srv/foo/bar.html");
    next_response = expected_response = &response2;
    translate_cache(*pool, *cache, request2,
                    my_translate_handler, nullptr, cancel_ptr);

    const auto request3 = MakeRequest("/foo/index.html");
    const auto response3 = MakeResponse().Base("/foo/").File("/srv/foo/index.html");
    next_response = nullptr;
    expected_response = &response3;
    translate_cache(*pool, *cache, request3,
                    my_translate_handler, nullptr, cancel_ptr);

    const auto request4 = MakeRequest("/foo/");
    const auto response4 = MakeResponse().Base("/foo/").File("/srv/foo/");
    expected_response = &response4;
    translate_cache(*pool, *cache, request4,
                    my_translate_handler, nullptr, cancel_ptr);

    const auto request5 = MakeRequest("/foo");
    expected_response = nullptr;
    translate_cache(*pool, *cache, request5,
                    my_translate_handler, nullptr, cancel_ptr);

    const auto request10 = MakeRequest("/foo//bar");
    const auto response10 = MakeResponse().Base("/foo/").File("/srv/foo//bar");
    expected_response = &response10;
    translate_cache(*pool, *cache, request10,
                    my_translate_handler, nullptr, cancel_ptr);

    const auto request6 = MakeRequest("/cgi1/foo");
    const auto response6 = MakeResponse().Base("/cgi1/")
        .Cgi("/usr/lib/cgi-bin/cgi.pl", "/cgi1/foo", "x/foo");

    next_response = expected_response = &response6;
    translate_cache(*pool, *cache, request6,
                    my_translate_handler, nullptr, cancel_ptr);

    const auto request7 = MakeRequest("/cgi1/a/b/c");
    const auto response7 = MakeResponse().Base("/cgi1/")
        .Cgi("/usr/lib/cgi-bin/cgi.pl", "/cgi1/a/b/c", "x/a/b/c");

    next_response = nullptr;
    expected_response = &response7;
    translate_cache(*pool, *cache, request7,
                    my_translate_handler, nullptr, cancel_ptr);

    const auto request8 = MakeRequest("/cgi2/foo");
    const auto response8 = MakeResponse().Base("/cgi2/")
        .Cgi("/usr/lib/cgi-bin/cgi.pl", "/cgi2/foo", "foo");

    next_response = expected_response = &response8;
    translate_cache(*pool, *cache, request8,
                    my_translate_handler, nullptr, cancel_ptr);

    const auto request9 = MakeRequest("/cgi2/a/b/c");
    const auto response9 = MakeResponse().Base("/cgi2/")
        .Cgi("/usr/lib/cgi-bin/cgi.pl", "/cgi2/a/b/c", "a/b/c");

    next_response = nullptr;
    expected_response = &response9;
    translate_cache(*pool, *cache, request9,
                    my_translate_handler, nullptr, cancel_ptr);
}

/**
 * Feed the cache with a request to the BASE.  This was buggy until
 * 4.0.30.
 */
TEST(TranslationCache, BaseRoot)
{
    Instance instance;
    struct pool *pool = instance.root_pool;
    auto *cache = instance.cache;

    CancellablePointer cancel_ptr;

    const auto request1 = MakeRequest("/base_root/");
    const auto response1 = MakeResponse().Base("/base_root/").File("/var/www/");
    next_response = expected_response = &response1;
    translate_cache(*pool, *cache, request1,
                    my_translate_handler, nullptr, cancel_ptr);

    const auto request2 = MakeRequest("/base_root/hansi");
    const auto response2 = MakeResponse().Base("/base_root/").File("/var/www/hansi");
    next_response = nullptr;
    expected_response = &response2;
    translate_cache(*pool, *cache, request2,
                    my_translate_handler, nullptr, cancel_ptr);
}

TEST(TranslationCache, BaseMismatch)
{
    Instance instance;
    struct pool *pool = instance.root_pool;
    auto *cache = instance.cache;

    CancellablePointer cancel_ptr;

    const auto request1 = MakeRequest("/base_mismatch/hansi");
    const auto response1 = MakeResponse().Base("/different_base/").File("/var/www/");

    next_response = &response1;
    expected_response = nullptr;
    translate_cache(*pool, *cache, request1,
                    my_translate_handler, nullptr, cancel_ptr);
}

/**
 * Test BASE+URI.
 */
TEST(TranslationCache, BaseUri)
{
    Instance instance;
    struct pool *pool = instance.root_pool;
    auto *cache = instance.cache;

    CancellablePointer cancel_ptr;

    const auto request1 = MakeRequest("/base_uri/foo");
    const auto response1 = MakeResponse().Base("/base_uri/")
        .File("/var/www/foo")
        .Uri("/modified/foo");

    next_response = expected_response = &response1;
    translate_cache(*pool, *cache, request1,
                    my_translate_handler, nullptr, cancel_ptr);

    const auto request2 = MakeRequest("/base_uri/hansi");
    const auto response2 = MakeResponse().Base("/base_uri/")
        .File("/var/www/hansi")
        .Uri("/modified/hansi");

    next_response = nullptr;
    expected_response = &response2;
    translate_cache(*pool, *cache, request2,
                    my_translate_handler, nullptr, cancel_ptr);
}

/**
 * Test BASE+REDIRECT.
 */
TEST(TranslationCache, BaseRedirect)
{
    Instance instance;
    struct pool *pool = instance.root_pool;
    auto *cache = instance.cache;

    CancellablePointer cancel_ref;

    const auto request1 = MakeRequest("/base_redirect/foo");
    const auto response1 = MakeResponse().Base("/base_redirect/")
        .File("/var/www/foo")
        .Redirect("http://modified/foo");

    next_response = expected_response = &response1;
    translate_cache(*pool, *cache, request1,
                    my_translate_handler, nullptr, cancel_ref);

    const auto request2 = MakeRequest("/base_redirect/hansi");
    const auto response2 = MakeResponse().Base("/base_redirect/")
        .File("/var/www/hansi")
        .Redirect("http://modified/hansi");

    next_response = nullptr;
    expected_response = &response2;
    translate_cache(*pool, *cache, request2,
                    my_translate_handler, nullptr, cancel_ref);
}

/**
 * Test BASE+TEST_PATH.
 */
TEST(TranslationCache, BaseTestPath)
{
    Instance instance;
    struct pool *pool = instance.root_pool;
    auto *cache = instance.cache;

    CancellablePointer cancel_ptr;

    const auto request1 = MakeRequest("/base_test_path/foo");
    const auto response1 = MakeResponse().Base("/base_test_path/")
        .File("/var/www/foo")
        .TestPath("/modified/foo");

    next_response = expected_response = &response1;
    translate_cache(*pool, *cache, request1,
                    my_translate_handler, nullptr, cancel_ptr);

    const auto request2 = MakeRequest("/base_test_path/hansi");
    const auto response2 = MakeResponse().Base("/base_test_path/")
        .File("/var/www/hansi")
        .TestPath("/modified/hansi");

    next_response = nullptr;
    expected_response = &response2;
    translate_cache(*pool, *cache, request2,
                    my_translate_handler, nullptr, cancel_ptr);
}

TEST(TranslationCache, EasyBase)
{
    Instance instance;
    struct pool *pool = instance.root_pool;
    auto *cache = instance.cache;

    const auto request1 = MakeRequest("/easy/bar.html");

    const auto response1 = MakeResponse().EasyBase("/easy/").File("/var/www/");
    const auto response1b = MakeResponse().EasyBase("/easy/").File("/var/www/bar.html");

    CancellablePointer cancel_ptr;

    next_response = &response1;
    expected_response = &response1b;
    translate_cache(*pool, *cache, request1,
                    my_translate_handler, nullptr, cancel_ptr);

    next_response = nullptr;
    translate_cache(*pool, *cache, request1,
                    my_translate_handler, nullptr, cancel_ptr);

    const auto request2 = MakeRequest("/easy/index.html");
    const auto response2 = MakeResponse().EasyBase("/easy/")
        .File("/var/www/index.html");
    expected_response = &response2;
    translate_cache(*pool, *cache, request2,
                    my_translate_handler, nullptr, cancel_ptr);
}

/**
 * Test EASY_BASE+URI.
 */
TEST(TranslationCache, EasyBaseUri)
{
    Instance instance;
    struct pool *pool = instance.root_pool;
    auto *cache = instance.cache;

    CancellablePointer cancel_ptr;

    const auto request1 = MakeRequest("/easy_base_uri/foo");
    const auto response1 = MakeResponse().EasyBase("/easy_base_uri/")
        .File("/var/www/")
        .Uri("/modified/");
    const auto response1b = MakeResponse().EasyBase("/easy_base_uri/")
        .File("/var/www/foo")
        .Uri("/modified/foo");

    next_response = &response1;
    expected_response = &response1b;
    translate_cache(*pool, *cache, request1,
                    my_translate_handler, nullptr, cancel_ptr);

    const auto request2 = MakeRequest("/easy_base_uri/hansi");
    const auto response2 = MakeResponse().EasyBase("/easy_base_uri/")
        .File("/var/www/hansi")
        .Uri("/modified/hansi");

    next_response = nullptr;
    expected_response = &response2;
    translate_cache(*pool, *cache, request2,
                    my_translate_handler, nullptr, cancel_ptr);
}

/**
 * Test EASY_BASE + TEST_PATH.
 */
TEST(TranslationCache, EasyBaseTestPath)
{
    Instance instance;
    struct pool *pool = instance.root_pool;
    auto *cache = instance.cache;

    CancellablePointer cancel_ptr;

    const auto request1 = MakeRequest("/easy_base_test_path/foo");
    const auto response1 = MakeResponse().EasyBase("/easy_base_test_path/")
        .File("/var/www/")
        .TestPath("/modified/");
    const auto response1b = MakeResponse().EasyBase("/easy_base_test_path/")
        .File("/var/www/foo")
        .TestPath("/modified/foo");

    next_response = &response1;
    expected_response = &response1b;
    translate_cache(*pool, *cache, request1,
                    my_translate_handler, nullptr, cancel_ptr);

    const auto request2 = MakeRequest("/easy_base_test_path/hansi");
    const auto response2 = MakeResponse().EasyBase("/easy_base_test_path/")
        .File("/var/www/hansi")
        .TestPath("/modified/hansi");

    next_response = nullptr;
    expected_response = &response2;
    translate_cache(*pool, *cache, request2,
                    my_translate_handler, nullptr, cancel_ptr);
}

TEST(TranslationCache, VaryInvalidate)
{
    Instance instance;
    struct pool *pool = instance.root_pool;
    auto *cache = instance.cache;

    static const TranslationCommand response5_vary[] = {
        TranslationCommand::QUERY_STRING,
    };

    static const TranslationCommand response5_invalidate[] = {
        TranslationCommand::QUERY_STRING,
    };

    const auto response5c = MakeResponse().File("/srv/qs3")
        .Vary(response5_vary).Invalidate(response5_invalidate);

    CancellablePointer cancel_ptr;

    const auto request6 = MakeRequest("/qs").QueryString("abc");
    const auto response5a = MakeResponse().File("/srv/qs1")
        .Vary(response5_vary);
    next_response = expected_response = &response5a;
    translate_cache(*pool, *cache, request6,
                    my_translate_handler, nullptr, cancel_ptr);

    const auto request7 = MakeRequest("/qs").QueryString("xyz");
    const auto response5b = MakeResponse().File("/srv/qs2")
        .Vary(response5_vary);
    next_response = expected_response = &response5b;
    translate_cache(*pool, *cache, request7,
                    my_translate_handler, nullptr, cancel_ptr);

    next_response = nullptr;
    expected_response = &response5a;
    translate_cache(*pool, *cache, request6,
                    my_translate_handler, nullptr, cancel_ptr);

    next_response = nullptr;
    expected_response = &response5b;
    translate_cache(*pool, *cache, request7,
                    my_translate_handler, nullptr, cancel_ptr);

    const auto request8 = MakeRequest("/qs/").QueryString("xyz");
    next_response = expected_response = &response5c;
    translate_cache(*pool, *cache, request8,
                    my_translate_handler, nullptr, cancel_ptr);

    next_response = nullptr;
    expected_response = &response5a;
    translate_cache(*pool, *cache, request6,
                    my_translate_handler, nullptr, cancel_ptr);

    next_response = expected_response = &response5c;
    translate_cache(*pool, *cache, request7,
                    my_translate_handler, nullptr, cancel_ptr);

    next_response = expected_response = &response5c;
    translate_cache(*pool, *cache, request8,
                    my_translate_handler, nullptr, cancel_ptr);

    expected_response = &response5c;
    translate_cache(*pool, *cache, request7,
                    my_translate_handler, nullptr, cancel_ptr);
}

TEST(TranslationCache, InvalidateUri)
{
    Instance instance;
    struct pool *pool = instance.root_pool;
    auto *cache = instance.cache;

    CancellablePointer cancel_ptr;

    /* feed the cache */

    const auto request1 = MakeRequest("/invalidate/uri");
    const auto response1 = MakeResponse().File("/var/www/invalidate/uri");

    next_response = expected_response = &response1;
    translate_cache(*pool, *cache, request1,
                    my_translate_handler, nullptr, cancel_ptr);

    const auto request2 = MakeRequest("/invalidate/uri").Check("x");
    const auto response2 = MakeResponse().File("/var/www/invalidate/uri");
    next_response = expected_response = &response2;
    translate_cache(*pool, *cache, request2,
                    my_translate_handler, nullptr, cancel_ptr);

    const auto request3 = MakeRequest("/invalidate/uri")
        .ErrorDocumentStatus(HTTP_STATUS_INTERNAL_SERVER_ERROR);
    const auto response3 = MakeResponse().File("/var/www/500/invalidate/uri");
    next_response = expected_response = &response3;
    translate_cache(*pool, *cache, request3,
                    my_translate_handler, nullptr, cancel_ptr);

    const auto request4 = MakeRequest("/invalidate/uri")
        .ErrorDocumentStatus(HTTP_STATUS_INTERNAL_SERVER_ERROR)
        .Check("x");
    const auto response4 = MakeResponse().File("/var/www/500/check/invalidate/uri");

    next_response = expected_response = &response4;
    translate_cache(*pool, *cache, request4,
                    my_translate_handler, nullptr, cancel_ptr);

    const auto request4b = MakeRequest("/invalidate/uri")
        .ErrorDocumentStatus(HTTP_STATUS_INTERNAL_SERVER_ERROR)
        .Check("x")
        .WantFullUri({ "a\0/b", 4 });
    const auto response4b = MakeResponse().File("/var/www/500/check/wfu/invalidate/uri");
    next_response = expected_response = &response4b;
    translate_cache(*pool, *cache, request4b,
                    my_translate_handler, nullptr, cancel_ptr);

    /* verify the cache items */

    next_response = nullptr;

    expected_response = &response1;
    translate_cache(*pool, *cache, request1,
                    my_translate_handler, nullptr, cancel_ptr);

    expected_response = &response2;
    translate_cache(*pool, *cache, request2,
                    my_translate_handler, nullptr, cancel_ptr);

    expected_response = &response3;
    translate_cache(*pool, *cache, request3,
                    my_translate_handler, nullptr, cancel_ptr);

    expected_response = &response4;
    translate_cache(*pool, *cache, request4,
                    my_translate_handler, nullptr, cancel_ptr);

    expected_response = &response4b;
    translate_cache(*pool, *cache, request4b,
                    my_translate_handler, nullptr, cancel_ptr);

    /* invalidate all cache items */

    const auto request5 = MakeRequest("/invalidate/uri")
        .ErrorDocumentStatus(HTTP_STATUS_NOT_FOUND);
    static const TranslationCommand response5_invalidate[] = {
        TranslationCommand::URI,
    };
    const auto response5 = MakeResponse().File("/var/www/404/invalidate/uri")
        .Invalidate(response5_invalidate);

    next_response = expected_response = &response5;
    translate_cache(*pool, *cache, request5,
                    my_translate_handler, nullptr, cancel_ptr);

    /* check if all cache items have really been deleted */

    next_response = expected_response = nullptr;

    translate_cache(*pool, *cache, request1,
                    my_translate_handler, nullptr, cancel_ptr);
    translate_cache(*pool, *cache, request2,
                    my_translate_handler, nullptr, cancel_ptr);
    translate_cache(*pool, *cache, request3,
                    my_translate_handler, nullptr, cancel_ptr);
    translate_cache(*pool, *cache, request4,
                    my_translate_handler, nullptr, cancel_ptr);
    translate_cache(*pool, *cache, request4b,
                    my_translate_handler, nullptr, cancel_ptr);
}

TEST(TranslationCache, Regex)
{
    Instance instance;
    struct pool *pool = instance.root_pool;
    auto *cache = instance.cache;

    CancellablePointer cancel_ptr;

    /* add the "inverse_regex" test to the cache first */
    const auto request_i1 = MakeRequest("/regex/foo");
    const auto response_i1 = MakeResponse().File("/var/www/regex/other/foo")
        .Base("/regex/").InverseRegex("\\.(jpg|html)$");
    next_response = expected_response = &response_i1;
    translate_cache(*pool, *cache, request_i1,
                    my_translate_handler, nullptr, cancel_ptr);

    /* fill the cache */
    const auto request1 = MakeRequest("/regex/a/foo.jpg");
    const auto response1 = MakeResponse().File("/var/www/regex/images/a/foo.jpg")
        .Base("/regex/").Regex("\\.jpg$");
    next_response = expected_response = &response1;
    translate_cache(*pool, *cache, request1,
                    my_translate_handler, nullptr, cancel_ptr);

    /* regex mismatch */
    const auto request2 = MakeRequest("/regex/b/foo.html");
    const auto response2 = MakeResponse().File("/var/www/regex/html/b/foo.html")
        .Base("/regex/").Regex("\\.html$");
    next_response = expected_response = &response2;
    translate_cache(*pool, *cache, request2,
                    my_translate_handler, nullptr, cancel_ptr);

    /* regex match */
    const auto request3 = MakeRequest("/regex/c/bar.jpg");
    const auto response3 = MakeResponse().File("/var/www/regex/images/c/bar.jpg")
        .Base("/regex/").Regex("\\.jpg$");
    next_response = nullptr;
    expected_response = &response3;
    translate_cache(*pool, *cache, request3,
                    my_translate_handler, nullptr, cancel_ptr);

    /* second regex match */
    const auto request4 = MakeRequest("/regex/d/bar.html");
    const auto response4 = MakeResponse().File("/var/www/regex/html/d/bar.html")
        .Base("/regex/").Regex("\\.html$");
    next_response = nullptr;
    expected_response = &response4;
    translate_cache(*pool, *cache, request4,
                    my_translate_handler, nullptr, cancel_ptr);

    /* see if the "inverse_regex" cache item is still there */
    const auto request_i2 = MakeRequest("/regex/bar");
    const auto response_i2 = MakeResponse().File("/var/www/regex/other/bar")
        .Base("/regex/").InverseRegex("\\.(jpg|html)$");
    next_response = nullptr;
    expected_response = &response_i2;
    translate_cache(*pool, *cache, request_i2,
                    my_translate_handler, nullptr, cancel_ptr);
}

TEST(TranslationCache, RegexError)
{
    Instance instance;
    struct pool *pool = instance.root_pool;
    auto *cache = instance.cache;

    const auto request = MakeRequest("/regex-error");
    const auto response = MakeResponse().File("/error")
        .Base("/regex/").Regex("(");

    CancellablePointer cancel_ptr;

    /* this must fail */
    next_response = &response;
    expected_response = nullptr;
    translate_cache(*pool, *cache, request,
                    my_translate_handler, nullptr, cancel_ptr);
}

TEST(TranslationCache, RegexTail)
{
    Instance instance;
    struct pool *pool = instance.root_pool;
    auto *cache = instance.cache;

    CancellablePointer cancel_ptr;

    const auto request1 = MakeRequest("/regex_tail/a/foo.jpg");
    const auto response1 = MakeResponse().File("/var/www/regex/images/a/foo.jpg")
        .Base("/regex_tail/").RegexTail("^a/");
    next_response = expected_response = &response1;
    translate_cache(*pool, *cache, request1,
                    my_translate_handler, nullptr, cancel_ptr);

    const auto request2 = MakeRequest("/regex_tail/b/foo.html");
    next_response = expected_response = nullptr;
    translate_cache(*pool, *cache, request2,
                    my_translate_handler, nullptr, cancel_ptr);

    const auto request3 = MakeRequest("/regex_tail/a/bar.jpg");
    const auto response3 = MakeResponse().File("/var/www/regex/images/a/bar.jpg")
        .Base("/regex_tail/").RegexTail("^a/");
    next_response = nullptr;
    expected_response = &response3;
    translate_cache(*pool, *cache, request3,
                    my_translate_handler, nullptr, cancel_ptr);

    const auto request4 = MakeRequest("/regex_tail/%61/escaped.html");

    next_response = expected_response = nullptr;
    translate_cache(*pool, *cache, request4,
                    my_translate_handler, nullptr, cancel_ptr);
}

TEST(TranslationCache, RegexTailUnescape)
{
    Instance instance;
    struct pool *pool = instance.root_pool;
    auto *cache = instance.cache;

    CancellablePointer cancel_ptr;

    const auto request1 = MakeRequest("/regex_unescape/a/foo.jpg");
    const auto response1 = MakeResponse().File("/var/www/regex/images/a/foo.jpg")
        .Base("/regex_unescape/").RegexTailUnescape("^a/");

    next_response = expected_response = &response1;
    translate_cache(*pool, *cache, request1,
                    my_translate_handler, nullptr, cancel_ptr);

    const auto request2 = MakeRequest("/regex_unescape/b/foo.html");

    next_response = expected_response = nullptr;
    translate_cache(*pool, *cache, request2,
                    my_translate_handler, nullptr, cancel_ptr);

    const auto request3 = MakeRequest("/regex_unescape/a/bar.jpg");
    const auto response3 = MakeResponse().File("/var/www/regex/images/a/bar.jpg")
        .Base("/regex_unescape/").RegexTailUnescape("^a/");

    next_response = nullptr;
    expected_response = &response3;
    translate_cache(*pool, *cache, request3,
                    my_translate_handler, nullptr, cancel_ptr);

    const auto request4 = MakeRequest("/regex_unescape/%61/escaped.html");
    const auto response4 = MakeResponse().File("/var/www/regex/images/a/escaped.html")
        .Base("/regex_unescape/").RegexTailUnescape("^a/");
    next_response = nullptr;
    expected_response = &response4;
    translate_cache(*pool, *cache, request4,
                    my_translate_handler, nullptr, cancel_ptr);
}

TEST(TranslationCache, Expand)
{
    Instance instance;
    struct pool *pool = instance.root_pool;
    auto *cache = instance.cache;

    CancellablePointer cancel_ptr;

    /* add to cache */

    const auto request1 = MakeRequest("/regex-expand/b=c");
    const auto response1n = MakeResponse()
        .Base("/regex-expand/").Regex("^/regex-expand/(.+=.+)$")
        .Cgi(MakeCgiAddress("/usr/lib/cgi-bin/foo.cgi").ExpandPathInfo("/a/\\1"));

    const auto response1e = MakeResponse()
        .Base("/regex-expand/").Regex("^/regex-expand/(.+=.+)$")
        .Cgi(MakeCgiAddress("/usr/lib/cgi-bin/foo.cgi", nullptr,
                            "/a/b=c"));

    next_response = &response1n;
    expected_response = &response1e;
    translate_cache(*pool, *cache, request1,
                    my_translate_handler, nullptr, cancel_ptr);

    /* check match */

    const auto request2 = MakeRequest("/regex-expand/d=e");
    const auto response2 = MakeResponse()
        .Base("/regex-expand/").Regex("^/regex-expand/(.+=.+)$")
        .Cgi(MakeCgiAddress("/usr/lib/cgi-bin/foo.cgi", nullptr,
                            "/a/d=e"));

    next_response = nullptr;
    expected_response = &response2;
    translate_cache(*pool, *cache, request2,
                    my_translate_handler, nullptr, cancel_ptr);
}

TEST(TranslationCache, ExpandLocal)
{
    Instance instance;
    struct pool *pool = instance.root_pool;
    auto *cache = instance.cache;

    CancellablePointer cancel_ptr;

    /* add to cache */

    const auto request1 = MakeRequest("/regex-expand2/foo/bar.jpg/b=c");
    const auto response1n = MakeResponse()
        .Base("/regex-expand2/")
        .Regex("^/regex-expand2/(.+\\.jpg)/([^/]+=[^/]+)$")
        .File(MakeFileAddress("/dummy").ExpandPath("/var/www/\\1"));

    const auto response1e = MakeResponse()
        .Base("/regex-expand2/")
        .Regex("^/regex-expand2/(.+\\.jpg)/([^/]+=[^/]+)$")
        .File(MakeFileAddress("/var/www/foo/bar.jpg"));

    next_response = &response1n;
    expected_response = &response1e;
    translate_cache(*pool, *cache, request1,
                    my_translate_handler, nullptr, cancel_ptr);

    /* check match */

    const auto request2 = MakeRequest("/regex-expand2/x/y/z.jpg/d=e");
    const auto response2 = MakeResponse()
        .Base("/regex-expand2/")
        .Regex("^/regex-expand2/(.+\\.jpg)/([^/]+=[^/]+)$")
        .File("/var/www/x/y/z.jpg");

    next_response = nullptr;
    expected_response = &response2;
    translate_cache(*pool, *cache, request2,
                    my_translate_handler, nullptr, cancel_ptr);
}

TEST(TranslationCache, ExpandLocalFilter)
{
    Instance instance;
    struct pool *pool = instance.root_pool;
    auto *cache = instance.cache;

    CancellablePointer cancel_ptr;

    /* add to cache */

    const auto request1 = MakeRequest("/regex-expand3/foo/bar.jpg/b=c");

    const auto response1n = MakeResponse()
        .Base("/regex-expand3/")
        .Regex("^/regex-expand3/(.+\\.jpg)/([^/]+=[^/]+)$")
        .Filter(MakeCgiAddress("/usr/lib/cgi-bin/image-processor.cgi").ExpandPathInfo("/\\2"))
        .File(MakeFileAddress("/dummy").ExpandPath("/var/www/\\1"));

    const auto response1e = MakeResponse()
        .Base("/regex-expand3/")
        .Regex("^/regex-expand3/(.+\\.jpg)/([^/]+=[^/]+)$")
        .Filter(MakeCgiAddress("/usr/lib/cgi-bin/image-processor.cgi", nullptr,
                               "/b=c"))
        .File(MakeFileAddress("/var/www/foo/bar.jpg"));

    next_response = &response1n;
    expected_response = &response1e;
    translate_cache(*pool, *cache, request1,
                    my_translate_handler, nullptr, cancel_ptr);

    /* check match */

    const auto request2 = MakeRequest("/regex-expand3/x/y/z.jpg/d=e");
    const auto response2 = MakeResponse()
        .Base("/regex-expand3/")
        .Regex("^/regex-expand3/(.+\\.jpg)/([^/]+=[^/]+)$")
        .Filter(MakeCgiAddress("/usr/lib/cgi-bin/image-processor.cgi", nullptr,
                               "/d=e"))
        .File("/var/www/x/y/z.jpg");

    next_response = nullptr;
    expected_response = &response2;
    translate_cache(*pool, *cache, request2,
                    my_translate_handler, nullptr, cancel_ptr);
}

TEST(TranslationCache, ExpandUri)
{
    Instance instance;
    struct pool *pool = instance.root_pool;
    auto *cache = instance.cache;

    CancellablePointer cancel_ptr;

    /* add to cache */

    const auto request1 = MakeRequest("/regex-expand4/foo/bar.jpg/b=c");
    const auto response1n = MakeResponse()
        .Base("/regex-expand4/")
        .Regex("^/regex-expand4/(.+\\.jpg)/([^/]+=[^/]+)$")
        .Http(MakeHttpAddress("/foo/bar.jpg").ExpandPath("/\\1"));
    const auto response1e = MakeResponse()
        .Base("/regex-expand4/")
        .Regex("^/regex-expand4/(.+\\.jpg)/([^/]+=[^/]+)$")
        .Http(MakeHttpAddress("/foo/bar.jpg"));

    next_response = &response1n;
    expected_response = &response1e;
    translate_cache(*pool, *cache, request1,
                    my_translate_handler, nullptr, cancel_ptr);

    /* check match */

    const auto request2 = MakeRequest("/regex-expand4/x/y/z.jpg/d=e");
    const auto response2 = MakeResponse()
        .Base("/regex-expand4/")
        .Regex("^/regex-expand4/(.+\\.jpg)/([^/]+=[^/]+)$")
        .Http(MakeHttpAddress("/x/y/z.jpg"));

    next_response = nullptr;
    expected_response = &response2;
    translate_cache(*pool, *cache, request2,
                    my_translate_handler, nullptr, cancel_ptr);
}

TEST(TranslationCache, AutoBase)
{
    Instance instance;
    struct pool *pool = instance.root_pool;
    auto *cache = instance.cache;

    CancellablePointer cancel_ptr;

    /* store response */

    const auto request1 = MakeRequest("/auto-base/foo.cgi/bar");
    const auto response1 = MakeResponse()
        .AutoBase()
        .Cgi("/usr/lib/cgi-bin/foo.cgi", "/auto-base/foo.cgi/bar", "/bar");

    next_response = expected_response = &response1;
    translate_cache(*pool, *cache, request1,
                    my_translate_handler, nullptr, cancel_ptr);

    /* check if BASE was auto-detected */

    const auto request2 = MakeRequest("/auto-base/foo.cgi/check");
    const auto response2 = MakeResponse()
        .AutoBase().Base("/auto-base/foo.cgi/")
        .Cgi("/usr/lib/cgi-bin/foo.cgi", "/auto-base/foo.cgi/check", "/check");

    next_response = nullptr;
    expected_response = &response2;
    translate_cache(*pool, *cache, request2,
                    my_translate_handler, nullptr, cancel_ptr);
}

/**
 * Test CHECK + BASE.
 */
TEST(TranslationCache, BaseCheck)
{
    Instance instance;
    struct pool *pool = instance.root_pool;
    auto *cache = instance.cache;

    CancellablePointer cancel_ptr;

    /* feed the cache */

    const auto request1 = MakeRequest("/a/b/c.html");
    const auto response1 = MakeResponse().Base("/a/").Check("x");

    next_response = expected_response = &response1;
    translate_cache(*pool, *cache, request1,
                    my_translate_handler, nullptr, cancel_ptr);

    const auto request2 = MakeRequest("/a/b/c.html").Check("x");
    const auto response2 = MakeResponse().Base("/a/b/")
        .File("/var/www/vol0/a/b/c.html");

    next_response = expected_response = &response2;
    translate_cache(*pool, *cache, request2,
                    my_translate_handler, nullptr, cancel_ptr);

    const auto request3 = MakeRequest("/a/d/e.html").Check("x");
    const auto response3 = MakeResponse().Base("/a/d/")
        .File("/var/www/vol1/a/d/e.html");

    next_response = expected_response = &response3;
    translate_cache(*pool, *cache, request3,
                    my_translate_handler, nullptr, cancel_ptr);

    /* now check whether the translate cache matches the BASE
       correctly */

    next_response = nullptr;

    const auto request4 = MakeRequest("/a/f/g.html");
    const auto response4 = MakeResponse().Base("/a/").Check("x");

    expected_response = &response4;
    translate_cache(*pool, *cache, request4,
                    my_translate_handler, nullptr, cancel_ptr);

    const auto request5 = MakeRequest("/a/b/0/1.html");

    translate_cache(*pool, *cache, request5,
                    my_translate_handler, nullptr, cancel_ptr);

    const auto request6 = MakeRequest("/a/b/0/1.html").Check("x");
    const auto response6 = MakeResponse().Base("/a/b/")
        .File("/var/www/vol0/a/b/0/1.html");

    expected_response = &response6;
    translate_cache(*pool, *cache, request6,
                    my_translate_handler, nullptr, cancel_ptr);

    const auto request7 = MakeRequest("/a/d/2/3.html").Check("x");
    const auto response7 = MakeResponse().Base("/a/d/")
        .File("/var/www/vol1/a/d/2/3.html");

    expected_response = &response7;
    translate_cache(*pool, *cache, request7,
                    my_translate_handler, nullptr, cancel_ptr);

    /* expect cache misses */

    expected_response = nullptr;

    const auto miss1 = MakeRequest("/a/f/g.html").Check("y");
    translate_cache(*pool, *cache, miss1,
                    my_translate_handler, nullptr, cancel_ptr);
}

/**
 * Test WANT_FULL_URI + BASE.
 */
TEST(TranslationCache, BaseWantFullUri)
{
    Instance instance;
    struct pool *pool = instance.root_pool;
    auto *cache = instance.cache;

    CancellablePointer cancel_ptr;

    /* feed the cache */

    const auto request1 = MakeRequest("/wfu/a/b/c.html");
    const auto response1 = MakeResponse().Base("/wfu/a/").WantFullUri("x");

    next_response = expected_response = &response1;
    translate_cache(*pool, *cache, request1,
                    my_translate_handler, nullptr, cancel_ptr);

    const auto request2 = MakeRequest("/wfu/a/b/c.html").WantFullUri("x");
    const auto response2 = MakeResponse().Base("/wfu/a/b/")
        .File("/var/www/vol0/a/b/c.html");

    next_response = expected_response = &response2;
    translate_cache(*pool, *cache, request2,
                    my_translate_handler, nullptr, cancel_ptr);

    const auto request3 = MakeRequest("/wfu/a/d/e.html").WantFullUri("x");
    const auto response3 = MakeResponse().Base("/wfu/a/d/")
        .File("/var/www/vol1/a/d/e.html");

    next_response = expected_response = &response3;
    translate_cache(*pool, *cache, request3,
                    my_translate_handler, nullptr, cancel_ptr);

    /* now check whether the translate cache matches the BASE
       correctly */

    next_response = nullptr;

    const auto request4 = MakeRequest("/wfu/a/f/g.html");
    const auto response4 = MakeResponse().Base("/wfu/a/").WantFullUri("x");

    expected_response = &response4;
    translate_cache(*pool, *cache, request4,
                    my_translate_handler, nullptr, cancel_ptr);

    const auto request5 = MakeRequest("/wfu/a/b/0/1.html");

    translate_cache(*pool, *cache, request5,
                    my_translate_handler, nullptr, cancel_ptr);

    const auto request6 = MakeRequest("/wfu/a/b/0/1.html").WantFullUri("x");
    const auto response6 = MakeResponse().Base("/wfu/a/b/")
        .File("/var/www/vol0/a/b/0/1.html");

    expected_response = &response6;
    translate_cache(*pool, *cache, request6,
                    my_translate_handler, nullptr, cancel_ptr);

    const auto request7 = MakeRequest("/wfu/a/d/2/3.html").WantFullUri("x");
    const auto response7 = MakeResponse().Base("/wfu/a/d/")
        .File("/var/www/vol1/a/d/2/3.html");

    expected_response = &response7;
    translate_cache(*pool, *cache, request7,
                    my_translate_handler, nullptr, cancel_ptr);

    /* expect cache misses */

    const auto miss1 = MakeRequest("/wfu/a/f/g.html").WantFullUri("y");
    expected_response = nullptr;
    translate_cache(*pool, *cache, miss1,
                    my_translate_handler, nullptr, cancel_ptr);
}

/**
 * Test UNSAFE_BASE.
 */
TEST(TranslationCache, UnsafeBase)
{
    Instance instance;
    struct pool *pool = instance.root_pool;
    auto *cache = instance.cache;

    CancellablePointer cancel_ptr;

    /* feed */
    const auto request1 = MakeRequest("/unsafe_base1/foo");
    const auto response1 = MakeResponse().Base("/unsafe_base1/")
        .File("/var/www/foo");

    next_response = expected_response = &response1;
    translate_cache(*pool, *cache, request1,
                    my_translate_handler, nullptr, cancel_ptr);

    const auto request2 = MakeRequest("/unsafe_base2/foo");
    const auto response2 = MakeResponse().UnsafeBase("/unsafe_base2/")
        .File("/var/www/foo");

    next_response = expected_response = &response2;
    translate_cache(*pool, *cache, request2,
                    my_translate_handler, nullptr, cancel_ptr);

    /* fail (no UNSAFE_BASE) */

    const auto request3 = MakeRequest("/unsafe_base1/../x");

    next_response = expected_response = nullptr;
    translate_cache(*pool, *cache, request3,
                    my_translate_handler, nullptr, cancel_ptr);

    /* success (with UNSAFE_BASE) */

    const auto request4 = MakeRequest("/unsafe_base2/../x");
    const auto response4 = MakeResponse().UnsafeBase("/unsafe_base2/")
        .File("/var/www/../x");

    next_response = nullptr;
    expected_response = &response4;
    translate_cache(*pool, *cache, request4,
                    my_translate_handler, nullptr, cancel_ptr);
}

/**
 * Test UNSAFE_BASE + EXPAND_PATH.
 */
TEST(TranslationCache, ExpandUnsafeBase)
{
    Instance instance;
    struct pool *pool = instance.root_pool;
    auto *cache = instance.cache;

    CancellablePointer cancel_ptr;

    /* feed */

    const auto request1 = MakeRequest("/expand_unsafe_base1/foo");
    const auto response1 = MakeResponse().Base("/expand_unsafe_base1/")
        .Regex("^/expand_unsafe_base1/(.*)$")
        .File(MakeFileAddress("/var/www/foo.html").ExpandPath("/var/www/\\1.html"));
    const auto response1e = MakeResponse().Base("/expand_unsafe_base1/")
        .Regex("^/expand_unsafe_base1/(.*)$")
        .File(MakeFileAddress("/var/www/foo.html"));

    next_response = &response1;
    expected_response = &response1e;
    translate_cache(*pool, *cache, request1,
                    my_translate_handler, nullptr, cancel_ptr);

    const auto request2 = MakeRequest("/expand_unsafe_base2/foo");
    const auto response2 = MakeResponse().UnsafeBase("/expand_unsafe_base2/")
        .Regex("^/expand_unsafe_base2/(.*)$")
        .File(MakeFileAddress("/var/www/foo.html").ExpandPath("/var/www/\\1.html"));
    const auto response2e = MakeResponse().UnsafeBase("/expand_unsafe_base2/")
        .Regex("^/expand_unsafe_base2/(.*)$")
        .File(MakeFileAddress("/var/www/foo.html"));

    next_response = &response2;
    expected_response = &response2e;
    translate_cache(*pool, *cache, request2,
                    my_translate_handler, nullptr, cancel_ptr);

    /* fail (no UNSAFE_BASE) */

    const auto request3 = MakeRequest("/expand_unsafe_base1/../x");

    next_response = expected_response = nullptr;
    translate_cache(*pool, *cache, request3,
                    my_translate_handler, nullptr, cancel_ptr);

    /* success (with UNSAFE_BASE) */

    const auto request4 = MakeRequest("/expand_unsafe_base2/../x");
    const auto response4 = MakeResponse().UnsafeBase("/expand_unsafe_base2/")
        .Regex("^/expand_unsafe_base2/(.*)$")
        .File(MakeFileAddress("/var/www/../x.html"));

    next_response = nullptr;
    expected_response = &response4;
    translate_cache(*pool, *cache, request4,
                    my_translate_handler, nullptr, cancel_ptr);
}

TEST(TranslationCache, ExpandBindMount)
{
    Instance instance;
    struct pool *pool = instance.root_pool;
    auto *cache = instance.cache;

    CancellablePointer cancel_ptr;

    /* add to cache */

    const auto request1 = MakeRequest("/expand_bind_mount/foo");

    const auto response1n = MakeResponse().Base("/expand_bind_mount/")
        .Regex("^/expand_bind_mount/(.+)$")
        .Cgi(MakeCgiAddress("/usr/lib/cgi-bin/foo.cgi")
             .BindMount("/home/\\1", "/mnt", true)
             .BindMount("/etc", "/etc"));

    const auto response1e = MakeResponse().Base("/expand_bind_mount/")
        .Regex("^/expand_bind_mount/(.+)$")
        .Cgi(MakeCgiAddress("/usr/lib/cgi-bin/foo.cgi")
             .BindMount("/home/foo", "/mnt")
             .BindMount("/etc", "/etc"));

    next_response = &response1n;
    expected_response = &response1e;
    translate_cache(*pool, *cache, request1,
                    my_translate_handler, nullptr, cancel_ptr);

    const auto request2 = MakeRequest("/expand_bind_mount/bar");
    const auto response2e = MakeResponse().Base("/expand_bind_mount/")
        .Regex("^/expand_bind_mount/(.+)$")
        .Cgi(MakeCgiAddress("/usr/lib/cgi-bin/foo.cgi")
             .BindMount("/home/bar", "/mnt")
             .BindMount("/etc", "/etc"));

    next_response = nullptr;
    expected_response = &response2e;
    translate_cache(*pool, *cache, request2,
                    my_translate_handler, nullptr, cancel_ptr);
}
