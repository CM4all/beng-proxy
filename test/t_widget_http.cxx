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
#include "widget/Widget.hxx"
#include "widget/Class.hxx"
#include "widget/Request.hxx"
#include "widget/LookupHandler.hxx"
#include "http_address.hxx"
#include "strmap.hxx"
#include "header_parser.hxx"
#include "PInstance.hxx"
#include "ResourceLoader.hxx"
#include "HttpResponseHandler.hxx"
#include "processor.hxx"
#include "css_processor.hxx"
#include "text_processor.hxx"
#include "penv.hxx"
#include "translation/Transformation.hxx"
#include "crash.hxx"
#include "istream/UnusedPtr.hxx"
#include "istream/istream.hxx"
#include "istream/istream_null.hxx"
#include "istream/istream_pipe.hxx"
#include "session_manager.hxx"
#include "session.hxx"
#include "suffix_registry.hxx"
#include "address_suffix_registry.hxx"
#include "util/Cancellable.hxx"
#include "util/PrintException.hxx"

#include "util/Compiler.h"

#include <sys/types.h>
#include <sys/socket.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <signal.h>

struct request {
    bool cached;
    http_method_t method;
    const char *uri;
    const char *request_headers;

    http_status_t status;
    const char *response_headers;
    const char *response_body;
};

static unsigned test_id;
static bool got_request, got_response;

bool
processable(gcc_unused const StringMap &headers)
{
    return false;
}

Istream *
processor_process(gcc_unused struct pool &pool, Istream &istream,
                  gcc_unused Widget &widget,
                  gcc_unused struct processor_env &env,
                  gcc_unused unsigned options)
{
    return &istream;
}

void
processor_lookup_widget(gcc_unused struct pool &pool,
                        gcc_unused Istream &istream,
                        gcc_unused Widget &widget,
                        gcc_unused const char *id,
                        gcc_unused struct processor_env &env,
                        gcc_unused unsigned options,
                        WidgetLookupHandler &handler,
                        gcc_unused CancellablePointer &cancel_ptr)
{
    handler.WidgetNotFound();
}

Istream *
css_processor(gcc_unused struct pool &pool, Istream &stream,
              gcc_unused Widget &widget,
              gcc_unused struct processor_env &env,
              gcc_unused unsigned options)
{
    return &stream;
}

bool
text_processor_allowed(gcc_unused const StringMap &headers)
{
    return false;
}

Istream *
text_processor(gcc_unused struct pool &pool, Istream &stream,
               gcc_unused const Widget &widget,
               gcc_unused const struct processor_env &env)
{
    return &stream;
}

bool
suffix_registry_lookup(gcc_unused struct pool &pool,
                       gcc_unused struct tcache &translate_cache,
                       gcc_unused const ResourceAddress &address,
                       gcc_unused const SuffixRegistryHandler &handler,
                       gcc_unused void *ctx,
                       gcc_unused CancellablePointer &cancel_ptr)
{
    return false;
}

struct tcache *global_translate_cache;

class Stock;
Stock *global_pipe_stock;

Istream *
istream_pipe_new(gcc_unused struct pool *pool, Istream &input,
                 gcc_unused Stock *pipe_stock)
{
    return &input;
}

class MyResourceLoader final : public ResourceLoader {
public:
    /* virtual methods from class ResourceLoader */
    void SendRequest(struct pool &pool,
                     sticky_hash_t,
                     http_method_t method,
                     const ResourceAddress &address,
                     http_status_t status, StringMap &&headers,
                     UnusedIstreamPtr body, const char *body_etag,
                     HttpResponseHandler &handler,
                     CancellablePointer &cancel_ptr) override;
};

void
MyResourceLoader::SendRequest(struct pool &pool,
                              sticky_hash_t,
                              http_method_t method,
                              gcc_unused const ResourceAddress &address,
                              gcc_unused http_status_t status,
                              StringMap &&headers,
                              UnusedIstreamPtr body,
                              gcc_unused const char *body_etag,
                              HttpResponseHandler &handler,
                              gcc_unused CancellablePointer &cancel_ptr)
{
    StringMap response_headers(pool);
    Istream *response_body = istream_null_new(&pool);
    const char *p;

    assert(!got_request);
    assert(method == HTTP_METHOD_GET);
    assert(!body);

    got_request = true;

    body.Clear();

    switch (test_id) {
    case 0:
        p = headers.Get("cookie");
        assert(p == nullptr);

        /* set one cookie */
        response_headers.Add("set-cookie", "foo=bar");
        break;

    case 1:
        /* is the one cookie present? */
        p = headers.Get("cookie");
        assert(p != nullptr);
        assert(strcmp(p, "foo=bar") == 0);

        /* add 2 more cookies */
        response_headers.Add("set-cookie", "a=b, c=d");
        break;

    case 2:
        /* are 3 cookies present? */
        p = headers.Get("cookie");
        assert(p != nullptr);
        assert(strcmp(p, "c=d; a=b; foo=bar") == 0);

        /* set two cookies in two headers */
        response_headers.Add("set-cookie", "e=f");
        response_headers.Add("set-cookie", "g=h");
        break;

    case 3:
        /* check for 5 cookies */
        p = headers.Get("cookie");
        assert(p != nullptr);
        assert(strcmp(p, "g=h; e=f; c=d; a=b; foo=bar") == 0);
        break;
    }

    handler.InvokeResponse(HTTP_STATUS_OK,
                           std::move(response_headers),
                           response_body);
}

struct Context final : HttpResponseHandler {
    /* virtual methods from class HttpResponseHandler */
    void OnHttpResponse(http_status_t status, StringMap &&headers,
                        Istream *body) noexcept override;
    void OnHttpError(std::exception_ptr ep) noexcept override;
};

void
Context::OnHttpResponse(http_status_t status, gcc_unused StringMap &&headers,
                        Istream *body) noexcept
{
    assert(!got_response);
    assert(status == 200);
    assert(body != nullptr);

    body->CloseUnused();

    got_response = true;
}

void gcc_noreturn
Context::OnHttpError(std::exception_ptr ep) noexcept
{
    PrintException(ep);

    assert(false);
}

static void
test_cookie_client(struct pool *pool)
{
    const auto address = MakeHttpAddress("/bar/").Host("foo");
    WidgetClass cls;
    cls.Init();
    cls.views.address = address;
    cls.stateful = true;

    CancellablePointer cancel_ptr;

    auto *session = session_new();

    MyResourceLoader resource_loader;
    struct processor_env env;
    env.resource_loader = &resource_loader;
    env.local_host = "localhost";
    env.remote_host = "localhost";
    env.request_headers = strmap_new(pool);
    env.session_id = session->id;
    env.realm = "foo";
    session_put(session);

    Widget widget(*pool, &cls);

    for (test_id = 0; test_id < 4; ++test_id) {
        got_request = false;
        got_response = false;

        Context context;
        widget_http_request(*pool, widget, env,
                            context, cancel_ptr);

        assert(got_request);
        assert(got_response);
    }
}

int main(int argc, char **argv) {
    (void)argc;
    (void)argv;

    PInstance instance;

    crash_global_init();
    session_manager_init(instance.event_loop, std::chrono::minutes(30), 0, 0);

    test_cookie_client(instance.root_pool);

    session_manager_deinit();
    crash_global_deinit();
}
