#include "delegate-client.h"
#include "delegate-stock.h"
#include "stock.h"
#include "async.h"
#include "lease.h"

#include <event.h>
#include <assert.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>

static const char helper_path[] = "./src/cm4all-beng-proxy-delegate-helper";
static struct hstock *delegate_stock;

static void
delegate_socket_release(bool reuse __attr_unused, void *ctx)
{
    struct stock_item *stock_item = ctx;

    hstock_put(delegate_stock, helper_path, stock_item, true);
}

static const struct lease delegate_socket_lease = {
    .release = delegate_socket_release,
};

static void
my_delegate_callback(int fd, void *ctx __attr_unused)
{
    if (fd < 0)
        fprintf(stderr, "%s\n", strerror(-fd));
    else
        close(fd);
}

static void
my_stock_callback(void *ctx, struct stock_item *item)
{
    pool_t pool = ctx;

    delegate_open(delegate_stock_item_get(item),
                  &delegate_socket_lease, item,
                  pool, "/etc/hosts",
                  my_delegate_callback, NULL);
}

int main(int argc __attr_unused, char **argv __attr_unused)
{
    struct event_base *event_base;
    pool_t root_pool, pool;
    struct async_operation_ref my_async_ref;

    event_base = event_init();

    root_pool = pool_new_libc(NULL, "root");
    delegate_stock = delegate_stock_new(root_pool);
    pool = pool_new_linear(root_pool, "test", 8192);

    hstock_get(delegate_stock, helper_path, NULL,
               my_stock_callback, pool, &my_async_ref);

    event_dispatch();
    hstock_free(&delegate_stock);

    pool_unref(pool);

    pool_unref(root_pool);
    pool_commit();

    pool_recycler_clear();

    event_base_free(event_base);
}
