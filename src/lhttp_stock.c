/*
 * Launch and manage "Local HTTP" child processes.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "lhttp_stock.h"
#include "lhttp_quark.h"
#include "lhttp_launch.h"
#include "lhttp_address.h"
#include "hstock.h"
#include "mstock.h"
#include "child_stock.h"
#include "stock.h"
#include "lease.h"
#include "child_manager.h"
#include "pevent.h"
#include "gerrno.h"

#include <daemon/log.h>

#include <glib.h>

#include <assert.h>
#include <unistd.h>
#include <string.h>

struct lhttp_stock {
    struct hstock *hstock;
    struct mstock *child_stock;
};

struct lhttp_connection {
    struct stock_item base;

    struct stock_item *child;

    struct lease_ref lease_ref;

    int fd;
    struct event event;
};

static const char *
lhttp_stock_key(struct pool *pool, const struct lhttp_address *address)
{
    return lhttp_address_server_id(pool, address);
}

/*
 * libevent callback
 *
 */

static void
lhttp_connection_event_callback(int fd, G_GNUC_UNUSED short event, void *ctx)
{
    struct lhttp_connection *connection = ctx;

    assert(fd == connection->fd);

    p_event_consumed(&connection->event, connection->base.pool);

    if ((event & EV_TIMEOUT) == 0) {
        char buffer;
        ssize_t nbytes = recv(fd, &buffer, sizeof(buffer), MSG_DONTWAIT);
        if (nbytes < 0)
            daemon_log(2, "error on idle LHTTP connection: %s\n",
                       strerror(errno));
        else if (nbytes > 0)
            daemon_log(2, "unexpected data from idle LHTTP connection\n");
    }

    stock_del(&connection->base);
    pool_commit();
}

/*
 * child_stock class
 *
 */

static int
lhttp_child_stock_run(gcc_unused struct pool *pool, gcc_unused const char *key,
                          void *info, gcc_unused void *ctx)
{
    const struct lhttp_address *address = info;

    lhttp_run(address, 0);
}

static const struct child_stock_class lhttp_child_stock_class = {
    .shutdown_signal = SIGTERM,
    .run = lhttp_child_stock_run,
};

/*
 * stock class
 *
 */

static struct pool *
lhttp_stock_pool(void *ctx gcc_unused, struct pool *parent,
               const char *uri gcc_unused)
{
    return pool_new_linear(parent, "lhttp_connection", 2048);
}

static void
lhttp_stock_create(void *ctx, struct stock_item *item,
                   const char *key, void *info,
                   gcc_unused struct pool *caller_pool,
                   gcc_unused struct async_operation_ref *async_ref)
{
    struct lhttp_stock *lhttp_stock = ctx;
    struct pool *pool = item->pool;
    const struct lhttp_address *address = info;
    struct lhttp_connection *connection = (struct lhttp_connection *)item;

    assert(key != NULL);
    assert(address != NULL);
    assert(address->path != NULL);

    GError *error = NULL;
    connection->child = mstock_get_now(lhttp_stock->child_stock, pool,
                                       key, info, address->concurrency,
                                       &connection->lease_ref,
                                       &error);

    connection->fd = child_stock_item_connect(connection->child, &error);

    if (connection->fd < 0) {
        g_prefix_error(&error, "failed to connect to LHTTP server '%s': ",
                       key);
        lease_release(&connection->lease_ref, false);
        stock_item_failed(item, error);
        return;
    }

    event_set(&connection->event, connection->fd, EV_READ|EV_TIMEOUT,
              lhttp_connection_event_callback, connection);

    stock_item_available(&connection->base);
}

static bool
lhttp_stock_borrow(void *ctx gcc_unused, struct stock_item *item)
{
    struct lhttp_connection *connection = (struct lhttp_connection *)item;

    p_event_del(&connection->event, connection->base.pool);
    return true;
}

static void
lhttp_stock_release(void *ctx gcc_unused, struct stock_item *item)
{
    struct lhttp_connection *connection = (struct lhttp_connection *)item;
    static const struct timeval tv = {
        .tv_sec = 300,
        .tv_usec = 0,
    };

    p_event_add(&connection->event, &tv, connection->base.pool,
                "lhttp_connection_event");
}

static void
lhttp_stock_destroy(gcc_unused void *ctx, struct stock_item *item)
{
    struct lhttp_connection *connection = (struct lhttp_connection *)item;

    p_event_del(&connection->event, connection->base.pool);
    close(connection->fd);

    lease_release(&connection->lease_ref, true);
}

static const struct stock_class lhttp_stock_class = {
    .item_size = sizeof(struct lhttp_connection),
    .pool = lhttp_stock_pool,
    .create = lhttp_stock_create,
    .borrow = lhttp_stock_borrow,
    .release = lhttp_stock_release,
    .destroy = lhttp_stock_destroy,
};


/*
 * interface
 *
 */

struct lhttp_stock *
lhttp_stock_new(struct pool *pool, unsigned limit, unsigned max_idle)
{
    struct lhttp_stock *lhttp_stock = p_malloc(pool, sizeof(*lhttp_stock));

    struct hstock *child_stock = child_stock_new(pool, limit, max_idle,
                                                 &lhttp_child_stock_class);
    lhttp_stock->child_stock = mstock_new(pool, child_stock);
    lhttp_stock->hstock = hstock_new(pool, &lhttp_stock_class, lhttp_stock,
                                     limit, max_idle);

    return lhttp_stock;
}

void
lhttp_stock_free(struct lhttp_stock *lhttp_stock)
{
    hstock_free(lhttp_stock->hstock);
    mstock_free(lhttp_stock->child_stock);
}

struct stock_item *
lhttp_stock_get(struct lhttp_stock *lhttp_stock, struct pool *pool,
                const struct lhttp_address *address,
                GError **error_r)
{
    if (address->jail.enabled && address->jail.home_directory == NULL) {
        g_set_error(error_r, lhttp_quark(), 0,
                    "No home directory for jailed LHTTP");
        return NULL;
    }

    union {
        const struct lhttp_address *in;
        void *out;
    } deconst = { .in = address };

    return hstock_get_now(lhttp_stock->hstock, pool,
                          lhttp_stock_key(pool, address),
                          deconst.out, error_r);
}

int
lhttp_stock_item_get_socket(const struct stock_item *item)
{
    const struct lhttp_connection *connection =
        (const struct lhttp_connection *)item;

    assert(connection->fd >= 0);

    return connection->fd;
}

enum istream_direct
lhttp_stock_item_get_type(gcc_unused const struct stock_item *item)
{
    return ISTREAM_SOCKET;
}

void
lhttp_stock_put(struct lhttp_stock *lhttp_stock, struct stock_item *item,
                bool destroy)
{
    struct lhttp_connection *connection = (struct lhttp_connection *)item;

    hstock_put(lhttp_stock->hstock, child_stock_item_key(connection->child),
               item, destroy);
}
