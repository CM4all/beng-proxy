/*
 * Copyright 2007-2019 Content Management AG
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

#include "widget/Inline.hxx"
#include "widget/Widget.hxx"
#include "widget/Request.hxx"
#include "widget/Resolver.hxx"
#include "bp/XmlProcessor.hxx"
#include "penv.hxx"
#include "uri/Dissect.hxx"
#include "HttpResponseHandler.hxx"
#include "istream/istream.hxx"
#include "istream/istream_iconv.hxx"
#include "pool/pool.hxx"
#include "PInstance.hxx"
#include "bp/session/Session.hxx"
#include "util/Cancellable.hxx"
#include "stopwatch.hxx"

#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>

const char *
Widget::GetLogName() const
{
    return "dummy";
}

StringView
Widget::LoggerDomain::GetDomain() const
{
    return "dummy";
}

UnusedIstreamPtr
istream_iconv_new(gcc_unused struct pool &pool, UnusedIstreamPtr input,
                  gcc_unused const char *tocode,
                  gcc_unused const char *fromcode) noexcept
{
    return input;
}

void
Widget::DiscardForFocused() noexcept
{
}

void
Widget::Cancel()
{
}

void
Widget::CheckHost(const char *, const char *) const
{
}

RealmSessionLease
processor_env::GetRealmSession() const
{
    return nullptr;
}

void
session_put(Session *session gcc_unused)
{
}

void
Widget::LoadFromSession(gcc_unused RealmSession &session)
{
}

void
widget_http_request(gcc_unused struct pool &pool,
                    gcc_unused Widget &widget,
                    gcc_unused struct processor_env &env,
                    const StopwatchPtr &,
                    HttpResponseHandler &handler,
                    gcc_unused CancellablePointer &cancel_ptr)
{
    handler.InvokeError(std::make_exception_ptr(std::runtime_error("Test")));
}

struct TestOperation final : Cancellable {
    /* virtual methods from class Cancellable */
    void Cancel() noexcept override {
    }
};

void
ResolveWidget(struct pool &pool,
              gcc_unused Widget &widget,
              gcc_unused TranslationService &service,
              gcc_unused WidgetResolverCallback callback,
              CancellablePointer &cancel_ptr) noexcept
{
    auto to = NewFromPool<TestOperation>(pool);
    cancel_ptr = *to;
}

static void
test_abort_resolver()
{
    PInstance instance;
    const char *uri;
    bool ret;
    DissectedUri dissected_uri;
    struct processor_env env;
    env.event_loop = &instance.event_loop;

    auto pool = pool_new_linear(instance.root_pool, "test", 4096);

    uri = "/beng.html";
    ret = dissected_uri.Parse(uri);
    if (!ret) {
        fprintf(stderr, "uri_parse() failed\n");
        exit(2);
    }

    Widget widget(pool, nullptr);

    auto istream = embed_inline_widget(*pool, env, nullptr, false, widget);
}

int main(int argc, char **argv) {
    (void)argc;
    (void)argv;

    test_abort_resolver();
}
