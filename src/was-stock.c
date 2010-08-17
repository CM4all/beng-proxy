/*
 * Launch and manage WAS child processes.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "was-stock.h"
#include "was-launch.h"
#include "child.h"
#include "async.h"
#include "client-socket.h"
#include "jail.h"
#include "pevent.h"

#include <daemon/log.h>

#include <glib.h>
#include <assert.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <signal.h>
#include <stdlib.h>

struct was_child_params {
    const char *executable_path;

    const char *jail_path;
};

struct was_child {
    struct stock_item base;

    const char *key;

    const char *jail_path;
    struct jail_config jail_config;

    struct was_process process;
    struct event event;
};

static const char *
was_stock_key(pool_t pool, const struct was_child_params *params)
{
    return params->jail_path == NULL
        ? params->executable_path
        : p_strcat(pool, params->executable_path, "|",
                   params->jail_path, NULL);
}

static void
was_child_callback(int status __attr_unused, void *ctx)
{
    struct was_child *child = ctx;

    child->process.pid = -1;
}

/*
 * libevent callback
 *
 */

static void
was_child_event_callback(int fd, G_GNUC_UNUSED short event, void *ctx)
{
    struct was_child *child = ctx;

    assert(fd == child->process.control_fd);

    p_event_consumed(&child->event, child->base.pool);

    if ((event & EV_TIMEOUT) == 0) {
        char buffer;
        ssize_t nbytes = recv(fd, &buffer, sizeof(buffer), MSG_DONTWAIT);
        if (nbytes < 0)
            daemon_log(2, "error on idle WAS control connection: %s\n",
                       strerror(errno));
        else if (nbytes > 0)
            daemon_log(2, "unexpected data from idle WAS control connection\n");
    }

    stock_del(&child->base);
    pool_commit();
}

/*
 * stock class
 *
 */

static pool_t
was_stock_pool(void *ctx __attr_unused, pool_t parent,
               const char *uri __attr_unused)
{
    return pool_new_linear(parent, "was_child", 2048);
}

static void
was_stock_create(G_GNUC_UNUSED void *ctx, struct stock_item *item,
                 const char *key, void *info,
                 pool_t caller_pool,
                 struct async_operation_ref *async_ref)
{
    pool_t pool = item->pool;
    struct was_child_params *params = info;
    struct was_child *child = (struct was_child *)item;

    (void)caller_pool;
    (void)async_ref;

    assert(key != NULL);
    assert(params != NULL);
    assert(params->executable_path != NULL);

    child->key = p_strdup(pool, key);

    if (params->jail_path != NULL) {
        child->jail_path = p_strdup(pool, params->jail_path);
        if (!jail_config_load(&child->jail_config,
                              "/etc/cm4all/jailcgi/jail.conf", pool)) {
            daemon_log(2, "Failed to load /etc/cm4all/jailcgi/jail.conf\n");
            stock_item_failed(item);
            return;
        }
    } else
        child->jail_path = NULL;

    if (!was_launch(&child->process, params->executable_path,
                    params->jail_path)) {
        stock_item_failed(item);
        return;
    }

    child_register(child->process.pid, was_child_callback, child);

    event_set(&child->event, child->process.control_fd, EV_READ|EV_TIMEOUT,
              was_child_event_callback, child);

    stock_item_available(&child->base);
}

static bool
was_stock_borrow(void *ctx __attr_unused, struct stock_item *item)
{
    struct was_child *child = (struct was_child *)item;

    p_event_del(&child->event, child->base.pool);
    return true;
}

static void
was_stock_release(void *ctx __attr_unused, struct stock_item *item)
{
    struct was_child *child = (struct was_child *)item;
    struct timeval tv = {
        .tv_sec = 300,
        .tv_usec = 0,
    };

    p_event_add(&child->event, &tv, child->base.pool, "was_child_event");
}

static void
was_stock_destroy(void *ctx __attr_unused, struct stock_item *item)
{
    struct was_child *child =
        (struct was_child *)item;

    if (child->process.pid >= 0) {
        kill(child->process.pid, SIGTERM);
        child_clear(child->process.pid);
    }

    if (child->process.control_fd >= 0) {
        p_event_del(&child->event, child->base.pool);
        close(child->process.control_fd);
    }

    close(child->process.input_fd);
    close(child->process.output_fd);
}

static const struct stock_class was_stock_class = {
    .item_size = sizeof(struct was_child),
    .pool = was_stock_pool,
    .create = was_stock_create,
    .borrow = was_stock_borrow,
    .release = was_stock_release,
    .destroy = was_stock_destroy,
};


/*
 * interface
 *
 */

struct hstock *
was_stock_new(pool_t pool, unsigned limit)
{
    return hstock_new(pool, &was_stock_class, NULL, limit);
}

void
was_stock_get(struct hstock *hstock, pool_t pool,
               const char *executable_path, const char *jail_path,
               stock_callback_t callback, void *callback_ctx,
               struct async_operation_ref *async_ref)
{
    struct was_child_params *params = p_malloc(pool, sizeof(*params));
    params->executable_path = executable_path;
    params->jail_path = jail_path;

    hstock_get(hstock, pool, was_stock_key(pool, params), params,
               callback, callback_ctx, async_ref);
}

const struct was_process *
was_stock_item_get(const struct stock_item *item)
{
    const struct was_child *child = (const struct was_child *)item;

    return &child->process;
}

void
was_stock_put(struct hstock *hstock, struct stock_item *item, bool destroy)
{
    struct was_child *child = (struct was_child *)item;

    hstock_put(hstock, child->key, item, destroy);
}
