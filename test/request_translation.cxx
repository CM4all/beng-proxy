#include "translation/Stock.hxx"
#include "translation/Handler.hxx"
#include "translation/Request.hxx"
#include "translation/Response.hxx"
#include "translation/Transformation.hxx"
#include "widget/View.hxx"
#include "fb_pool.hxx"
#include "pool.hxx"
#include "PInstance.hxx"
#include "lhttp_address.hxx"
#include "http_address.hxx"
#include "file_address.hxx"
#include "cgi_address.hxx"
#include "nfs/Address.hxx"
#include "net/AllocatedSocketAddress.hxx"
#include "util/Cancellable.hxx"
#include "util/PrintException.hxx"

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
        switch (address->GetHttp().protocol) {
        case HttpAddress::Protocol::HTTP:
            printf("http=%s\n", address->GetHttp().path);
            break;

        case HttpAddress::Protocol::AJP:
            printf("ajp=%s\n", address->GetHttp().path);
            break;
        }

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

static void
my_translate_response(TranslateResponse &response, void *ctx)
{
    const WidgetView *view;

    (void)ctx;

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
            }
        }
    }

    if (response.redirect != nullptr)
        printf("redirect=%s\n", response.redirect);
    if (response.session.IsNull())
        printf("session=%.*s\n", (int)response.session.size,
               (const char *)response.session.data);
    if (response.user != nullptr)
        printf("user=%s\n", response.user);
}

static void
my_translate_error(std::exception_ptr ep, gcc_unused void *ctx)
{
    PrintException(ep);
}

static const TranslateHandler my_translate_handler = {
    .response = my_translate_response,
    .error = my_translate_error,
};

int main(int argc, char **argv) {
    const ScopeFbPoolInit fb_pool_init;

    TranslateRequest request;
    request.Clear();
    request.host = "example.com";
    request.uri = "/foo/index.html";

    (void)argc;
    (void)argv;

    PInstance instance;

    AllocatedSocketAddress translation_socket;
    translation_socket.SetLocal("@translation");

    auto *translate_stock = tstock_new(instance.event_loop,
                                       translation_socket, 0);

    CancellablePointer cancel_ptr;
    tstock_translate(*translate_stock, instance.root_pool,
                     request, my_translate_handler, nullptr, cancel_ptr);

    instance.event_loop.Dispatch();
}
