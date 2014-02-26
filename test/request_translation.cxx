#include "tstock.h"
#include "translate-client.h"
#include "translate-request.h"
#include "translate-response.h"
#include "widget-view.h"
#include "transformation.h"
#include "balancer.h"
#include "tcp-stock.h"
#include "async.h"
#include "fb_pool.h"
#include "lhttp_address.h"

#include <stdio.h>
#include <event.h>

static void
print_resource_address(const struct resource_address *address)
{
    switch (address->type) {
    case RESOURCE_ADDRESS_NONE:
        break;

    case RESOURCE_ADDRESS_LOCAL:
        printf("path=%s\n", address->u.file->path);
        if (address->u.file->content_type != nullptr)
            printf("content_type=%s\n",
                   address->u.file->content_type);
        break;

    case RESOURCE_ADDRESS_HTTP:
        printf("proxy=%s\n", address->u.http->path);
        break;

    case RESOURCE_ADDRESS_LHTTP:
        printf("lhttp=%s|%s\n", address->u.lhttp->path, address->u.lhttp->uri);
        break;

    case RESOURCE_ADDRESS_PIPE:
        printf("pipe=%s\n", address->u.cgi->path);
        break;

    case RESOURCE_ADDRESS_CGI:
        printf("cgi=%s\n", address->u.cgi->path);
        break;

    case RESOURCE_ADDRESS_FASTCGI:
        printf("fastcgi=%s\n", address->u.cgi->path);
        break;

    case RESOURCE_ADDRESS_WAS:
        printf("was=%s\n", address->u.cgi->path);
        break;

    case RESOURCE_ADDRESS_AJP:
        printf("ajp=%s\n", address->u.http->path);
        break;

    case RESOURCE_ADDRESS_NFS:
        printf("nfs=%s:%s\n", address->u.nfs->server, address->u.nfs->path);
        break;
    }
}

static void
my_translate_response(const struct translate_response *response,
                      void *ctx)
{
    const struct widget_view *view;

    (void)ctx;

    if (response->status != 0)
        printf("status=%d\n", response->status);

    print_resource_address(&response->address);

    for (view = response->views; view != nullptr; view = view->next) {
        const struct transformation *transformation;

        if (view->name != nullptr)
            printf("view=%s\n", view->name);

        for (transformation = view->transformation; transformation != nullptr;
             transformation = transformation->next) {
            switch (transformation->type) {
            case transformation::TRANSFORMATION_PROCESS:
                printf("process\n");
                break;

            case transformation::TRANSFORMATION_PROCESS_CSS:
                printf("process_css\n");
                break;

            case transformation::TRANSFORMATION_PROCESS_TEXT:
                printf("process_text\n");
                break;

            case transformation::TRANSFORMATION_FILTER:
                printf("filter\n");
                print_resource_address(&transformation->u.filter);
                break;
            }
        }
    }

    if (response->redirect != nullptr)
        printf("redirect=%s\n", response->redirect);
    if (response->session != nullptr)
        printf("session=%s\n", response->session);
    if (response->user != nullptr)
        printf("user=%s\n", response->user);
}

static void
my_translate_error(GError *error, G_GNUC_UNUSED void *ctx)
{
    fprintf(stderr, "%s\n", error->message);
    g_error_free(error);
}

static const struct translate_handler my_translate_handler = {
    .response = my_translate_response,
    .error = my_translate_error,
};

int main(int argc, char **argv) {
    struct translate_request request = {
        .host = "example.com",
        .uri = "/foo/index.html",
    };
    struct pool *pool;
    struct hstock *tcp_stock;
    struct tstock *translate_stock;
    struct async_operation_ref async_ref;

    (void)argc;
    (void)argv;

    event_init();
    fb_pool_init(false);

    pool = pool_new_libc(nullptr, "root");

    tcp_stock = tcp_stock_new(pool, 0);
    translate_stock = tstock_new(pool, tcp_stock, "/tmp/beng-proxy-translate");

    tstock_translate(translate_stock, pool,
                     &request, &my_translate_handler, nullptr, &async_ref);

    event_dispatch();
    fb_pool_deinit();
}
