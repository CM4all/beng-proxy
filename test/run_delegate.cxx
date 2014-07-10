#include "delegate_glue.hxx"
#include "delegate_client.hxx"
#include "delegate_stock.hxx"
#include "hstock.hxx"
#include "async.h"
#include "defer.h"
#include "pool.h"

#include <event.h>
#include <assert.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>

static const char helper_path[] = "./src/cm4all-beng-proxy-delegate-helper";
static struct hstock *delegate_stock;
static struct pool *pool;

static void
my_stop(void *ctx gcc_unused)
{
    hstock_free(delegate_stock);
}

static void
my_delegate_callback(int fd, void *ctx gcc_unused)
{
    close(fd);

    defer(pool, my_stop, NULL, NULL);
}

static void
my_delegate_error(GError *error, gcc_unused void *ctx)
{
    g_printerr("%s\n", error->message);
    g_error_free(error);

    defer(pool, my_stop, NULL, NULL);
}

static const struct delegate_handler my_delegate_handler = {
    .success = my_delegate_callback,
    .error = my_delegate_error,
};

int main(int argc, char **argv)
{
    struct event_base *event_base;
    struct pool *root_pool;
    struct async_operation_ref my_async_ref;

    if (argc != 2) {
        fprintf(stderr, "usage: run-delegate PATH\n");
        return 1;
    }

    event_base = event_init();

    root_pool = pool_new_libc(NULL, "root");
    delegate_stock = delegate_stock_new(root_pool);
    pool = pool_new_linear(root_pool, "test", 8192);

    delegate_stock_open(delegate_stock, pool, helper_path, NULL,
                        argv[1],
                        &my_delegate_handler, NULL, &my_async_ref);

    event_dispatch();

    pool_unref(pool);

    pool_unref(root_pool);
    pool_commit();

    pool_recycler_clear();

    event_base_free(event_base);
}
