#include "delegate-glue.h"
#include "delegate-stock.h"
#include "stock.h"
#include "async.h"
#include "defer.h"

#include <event.h>
#include <assert.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>

static const char helper_path[] = "./src/cm4all-beng-proxy-delegate-helper";
static struct hstock *delegate_stock;
static pool_t pool;

static void
my_stop(void *ctx __attr_unused)
{
    hstock_free(&delegate_stock);
}

static void
my_delegate_callback(int fd, void *ctx __attr_unused)
{
    if (fd < 0)
        fprintf(stderr, "%s\n", strerror(-fd));
    else
        close(fd);

    defer(pool, my_stop, NULL, NULL);
}

int main(int argc, char **argv)
{
    struct event_base *event_base;
    pool_t root_pool;
    struct async_operation_ref my_async_ref;

    if (argc != 2) {
        fprintf(stderr, "usage: run-delegate PATH\n");
        return 1;
    }

    event_base = event_init();

    root_pool = pool_new_libc(NULL, "root");
    delegate_stock = delegate_stock_new(root_pool);
    pool = pool_new_linear(root_pool, "test", 8192);

    delegate_stock_open(delegate_stock, pool, helper_path, argv[1],
                        my_delegate_callback, NULL, &my_async_ref);

    event_dispatch();

    pool_unref(pool);

    pool_unref(root_pool);
    pool_commit();

    pool_recycler_clear();

    event_base_free(event_base);
}
