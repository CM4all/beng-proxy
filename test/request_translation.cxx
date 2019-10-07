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

#include "translation/Stock.hxx"
#include "translation/Handler.hxx"
#include "translation/Request.hxx"
#include "translation/Response.hxx"
#include "translation/Transformation.hxx"
#include "widget/View.hxx"
#include "fb_pool.hxx"
#include "pool/pool.hxx"
#include "PInstance.hxx"
#include "lhttp_address.hxx"
#include "http_address.hxx"
#include "file_address.hxx"
#include "cgi/Address.hxx"
#include "nfs/Address.hxx"
#include "net/AllocatedSocketAddress.hxx"
#include "util/Cancellable.hxx"
#include "util/PrintException.hxx"
#include "stopwatch.hxx"

#include <stdio.h>

static void
print_resource_address(const ResourceAddress *address)
{
    switch (address->type) {
    case ResourceAddress::Type::NONE:
        break;

    case ResourceAddress::Type::LOCAL:
        printf("path=%s\n", address->GetFile().path);
        if (address->GetFile().content_type != nullptr)
            printf("content_type=%s\n",
                   address->GetFile().content_type);
        break;

    case ResourceAddress::Type::HTTP:
        printf("http=%s\n", address->GetHttp().path);
        break;

    case ResourceAddress::Type::LHTTP:
        printf("lhttp=%s|%s\n", address->GetLhttp().path,
               address->GetLhttp().uri);
        break;

    case ResourceAddress::Type::PIPE:
        printf("pipe=%s\n", address->GetCgi().path);
        break;

    case ResourceAddress::Type::CGI:
        printf("cgi=%s\n", address->GetCgi().path);
        break;

    case ResourceAddress::Type::FASTCGI:
        printf("fastcgi=%s\n", address->GetCgi().path);
        break;

    case ResourceAddress::Type::WAS:
        printf("was=%s\n", address->GetCgi().path);
        break;

    case ResourceAddress::Type::NFS:
        printf("nfs=%s:%s\n", address->GetNfs().server,
               address->GetNfs().path);
        break;
    }
}

class MyHandler final : public TranslateHandler {
public:
    /* virtual methods from TranslateHandler */
    void OnTranslateResponse(TranslateResponse &response) noexcept override;
    void OnTranslateError(std::exception_ptr error) noexcept override;
};

void
MyHandler::OnTranslateResponse(TranslateResponse &response) noexcept
{
    const WidgetView *view;

    if (response.status != 0)
        printf("status=%d\n", response.status);

    print_resource_address(&response.address);

    for (view = response.views; view != nullptr; view = view->next) {
        if (view->name != nullptr)
            printf("view=%s\n", view->name);

        for (const Transformation *transformation = view->transformation;
             transformation != nullptr;
             transformation = transformation->next) {
            switch (transformation->type) {
            case Transformation::Type::PROCESS:
                printf("process\n");
                break;

            case Transformation::Type::PROCESS_CSS:
                printf("process_css\n");
                break;

            case Transformation::Type::PROCESS_TEXT:
                printf("process_text\n");
                break;

            case Transformation::Type::FILTER:
                printf("filter\n");
                print_resource_address(&transformation->u.filter.address);
                break;

            case Transformation::Type::SUBST:
                printf("subst '%s'\n", transformation->u.subst.yaml_file);
                break;
            }
        }
    }

    if (response.redirect != nullptr)
        printf("redirect=%s\n", response.redirect);
    if (!response.session.IsNull())
        printf("session=%.*s\n", (int)response.session.size,
               (const char *)response.session.data);
    if (response.user != nullptr)
        printf("user=%s\n", response.user);
}

void
MyHandler::OnTranslateError(std::exception_ptr ep) noexcept
{
    PrintException(ep);
}

int main(int argc, char **argv) {
    const ScopeFbPoolInit fb_pool_init;

    TranslateRequest request;
    request.host = "example.com";
    request.uri = "/foo/index.html";

    (void)argc;
    (void)argv;

    PInstance instance;

    AllocatedSocketAddress translation_socket;
    translation_socket.SetLocal("@translation");

    TranslationStock stock(instance.event_loop,
                           translation_socket, 0);

    MyHandler handler;
    CancellablePointer cancel_ptr;
    stock.SendRequest(instance.root_pool,
                      request, nullptr,
                      handler, cancel_ptr);

    instance.event_loop.Dispatch();
}
