#include "tstock.h"
#include "transformation.h"
#include "balancer.h"
#include "tcp-stock.h"
#include "async.h"
#include "config.h"

#include <stdio.h>
#include <event.h>

static void
print_resource_address(const struct resource_address *address)
{
    switch (address->type) {
    case RESOURCE_ADDRESS_NONE:
        break;

    case RESOURCE_ADDRESS_LOCAL:
        printf("path=%s\n", address->u.local.path);
        if (address->u.local.content_type != NULL)
            printf("content_type=%s\n",
                   address->u.local.content_type);
        break;

    case RESOURCE_ADDRESS_HTTP:
        printf("proxy=%s\n", address->u.http->uri);
        break;

    case RESOURCE_ADDRESS_PIPE:
        printf("pipe=%s\n", address->u.cgi.path);
        break;

    case RESOURCE_ADDRESS_CGI:
        printf("cgi=%s\n", address->u.cgi.path);
        break;

    case RESOURCE_ADDRESS_FASTCGI:
        printf("fastcgi=%s\n", address->u.cgi.path);
        break;

    case RESOURCE_ADDRESS_AJP:
        printf("ajp=%s\n", address->u.http->uri);
        break;
    }
}

static void
translate_callback(const struct translate_response *response,
                   void *ctx)
{
    const struct transformation_view *view;

    (void)ctx;

    if (response->status != 0)
        printf("status=%d\n", response->status);

    print_resource_address(&response->address);

    for (view = response->views; view != NULL; view = view->next) {
        const struct transformation *transformation;

        if (view->name != NULL)
            printf("view=%s\n", view->name);

        for (transformation = view->transformation; transformation != NULL;
             transformation = transformation->next) {
            switch (transformation->type) {
            case TRANSFORMATION_PROCESS:
                printf("process\n");
                break;

            case TRANSFORMATION_FILTER:
                printf("filter\n");
                print_resource_address(&transformation->u.filter);
                break;
            }
        }
    }

    if (response->redirect != NULL)
        printf("redirect=%s\n", response->redirect);
    if (response->session != NULL)
        printf("session=%s\n", response->session);
    if (response->user != NULL)
        printf("user=%s\n", response->user);
}

int main(int argc, char **argv) {
    struct translate_request request = {
        .host = "example.com",
        .uri = "/foo/index.html",
    };
    pool_t pool;
    struct hstock *tcp_stock;
    struct tstock *translate_stock;
    struct async_operation_ref async_ref;

    (void)argc;
    (void)argv;

    event_init();

    pool = pool_new_libc(NULL, "root");

    tcp_stock = tcp_stock_new(pool, balancer_new(pool), 0);
    translate_stock = tstock_new(pool, tcp_stock, "/tmp/beng-proxy-translate");

    tstock_translate(translate_stock, pool,
                     &request, translate_callback, NULL, &async_ref);

    event_dispatch();
}
