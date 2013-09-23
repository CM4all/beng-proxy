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
#include "stock.h"
#include "child.h"
#include "pevent.h"
#include "gerrno.h"

#include <daemon/log.h>

#include <glib.h>

#include <assert.h>
#include <unistd.h>

struct lhttp_child {
    struct stock_item base;

    const char *key;

    struct lhttp_process process;

    int fd;
    struct event event;
};

static const char *
lhttp_stock_key(struct pool *pool, const struct lhttp_address *address)
{
    return lhttp_address_server_id(pool, address);
}

static void
lhttp_child_callback(int status gcc_unused, void *ctx)
{
    struct lhttp_child *child = ctx;

    child->process.pid = -1;
}

/*
 * libevent callback
 *
 */

static void
lhttp_child_event_callback(int fd, G_GNUC_UNUSED short event, void *ctx)
{
    struct lhttp_child *child = ctx;

    assert(fd == child->fd);

    p_event_consumed(&child->event, child->base.pool);

    if ((event & EV_TIMEOUT) == 0) {
        char buffer;
        ssize_t nbytes = recv(fd, &buffer, sizeof(buffer), MSG_DONTWAIT);
        if (nbytes < 0)
            daemon_log(2, "error on idle LHTTP connection: %s\n",
                       strerror(errno));
        else if (nbytes > 0)
            daemon_log(2, "unexpected data from idle LHTTP connection\n");
    }

    stock_del(&child->base);
    pool_commit();
}

/*
 * stock class
 *
 */

static struct pool *
lhttp_stock_pool(void *ctx gcc_unused, struct pool *parent,
               const char *uri gcc_unused)
{
    return pool_new_linear(parent, "lhttp_child", 2048);
}

static void
lhttp_stock_create(G_GNUC_UNUSED void *ctx, struct stock_item *item,
                  const char *key, void *info,
                   gcc_unused struct pool *caller_pool,
                   gcc_unused struct async_operation_ref *async_ref)
{
    struct pool *pool = item->pool;
    const struct lhttp_address *address = info;
    struct lhttp_child *child = (struct lhttp_child *)item;

    assert(key != NULL);
    assert(address != NULL);
    assert(address->path != NULL);

    child->key = p_strdup(pool, key);

    GError *error = NULL;
    if (!lhttp_launch(&child->process, address, &error)) {
        stock_item_failed(item, error);
        return;
    }

    child_register(child->process.pid, key, lhttp_child_callback, child);

    child->fd = lhttp_process_connect(&child->process, &error);
    lhttp_process_unlink_socket(&child->process);

    if (child->fd < 0) {
        g_prefix_error(&error, "failed to connect to LHTTP server '%s': ",
                       child->key);

        child_kill(child->process.pid);
        stock_item_failed(item, error);
        return;
    }

    event_set(&child->event, child->fd, EV_READ|EV_TIMEOUT,
              lhttp_child_event_callback, child);

    stock_item_available(&child->base);
}

static bool
lhttp_stock_borrow(void *ctx gcc_unused, struct stock_item *item)
{
    struct lhttp_child *child = (struct lhttp_child *)item;

    p_event_del(&child->event, child->base.pool);
    return true;
}

static void
lhttp_stock_release(void *ctx gcc_unused, struct stock_item *item)
{
    struct lhttp_child *child = (struct lhttp_child *)item;
    static const struct timeval tv = {
        .tv_sec = 300,
        .tv_usec = 0,
    };

    p_event_add(&child->event, &tv, child->base.pool, "lhttp_child_event");
}

static void
lhttp_stock_destroy(void *ctx gcc_unused, struct stock_item *item)
{
    struct lhttp_child *child =
        (struct lhttp_child *)item;

    if (child->process.pid >= 0)
        child_kill(child->process.pid);

    if (child->fd >= 0) {
        p_event_del(&child->event, child->base.pool);
        close(child->fd);
    }
}

static const struct stock_class lhttp_stock_class = {
    .item_size = sizeof(struct lhttp_child),
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

struct hstock *
lhttp_stock_new(struct pool *pool, unsigned limit, unsigned max_idle)
{
    return hstock_new(pool, &lhttp_stock_class, NULL, limit, max_idle);
}

struct stock_item *
lhttp_stock_get(struct hstock *hstock, struct pool *pool,
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

    return hstock_get_now(hstock, pool, lhttp_stock_key(pool, address),
                          deconst.out, error_r);
}

int
lhttp_stock_item_get_socket(const struct stock_item *item)
{
    const struct lhttp_child *child = (const struct lhttp_child *)item;

    assert(child->fd >= 0);

    return child->fd;
}

enum istream_direct
lhttp_stock_item_get_type(gcc_unused const struct stock_item *item)
{
    return ISTREAM_SOCKET;
}

void
lhttp_stock_put(struct hstock *hstock, struct stock_item *item, bool destroy)
{
    struct lhttp_child *child = (struct lhttp_child *)item;

    hstock_put(hstock, child->key, item, destroy);
}
