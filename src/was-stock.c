/*
 * Launch and manage WAS child processes.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "was-stock.h"
#include "was-quark.h"
#include "was-launch.h"
#include "hstock.h"
#include "stock.h"
#include "child_manager.h"
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

    const char *const*args;
    unsigned n_args;

    const struct jail_params *jail;
    bool user_namespace, network_namespace;
};

struct was_child {
    struct stock_item base;

    const char *key;

    struct jail_params jail_params;

    struct jail_config jail_config;

    struct was_process process;
    struct event event;
};

static const char *
was_stock_key(struct pool *pool, const struct was_child_params *params)
{
    const char *key = params->executable_path;
    for (unsigned i = 0, n = params->n_args; i < n; ++i)
        key = p_strcat(pool, key, " ", params->args[i], NULL);

    if (params->jail != NULL && params->jail->enabled)
        key = p_strcat(pool, key, "|j=",
                       params->jail->home_directory, NULL);

    return key;
}

static void
was_child_callback(gcc_unused int status, void *ctx)
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

static struct pool *
was_stock_pool(gcc_unused void *ctx, struct pool *parent,
               gcc_unused const char *uri)
{
    return pool_new_linear(parent, "was_child", 2048);
}

static void
was_stock_create(G_GNUC_UNUSED void *ctx, struct stock_item *item,
                 const char *key, void *info,
                 struct pool *caller_pool,
                 struct async_operation_ref *async_ref)
{
    struct pool *pool = item->pool;
    struct was_child_params *params = info;
    struct was_child *child = (struct was_child *)item;

    (void)caller_pool;
    (void)async_ref;

    assert(key != NULL);
    assert(params != NULL);
    assert(params->executable_path != NULL);

    child->key = p_strdup(pool, key);

    if (params->jail != NULL && params->jail->enabled) {
        jail_params_copy(pool, &child->jail_params, params->jail);

        if (!jail_config_load(&child->jail_config,
                              "/etc/cm4all/jailcgi/jail.conf", pool)) {
            GError *error = g_error_new(was_quark(), 0,
                                        "Failed to load /etc/cm4all/jailcgi/jail.conf");
            stock_item_failed(item, error);
            return;
        }
    } else
        child->jail_params.enabled = false;

    GError *error = NULL;
    if (!was_launch(&child->process, params->executable_path,
                    params->args, params->n_args,
                    params->jail,
                    params->user_namespace, params->network_namespace,
                    &error)) {
        stock_item_failed(item, error);
        return;
    }

    child_register(child->process.pid, key, was_child_callback, child);

    event_set(&child->event, child->process.control_fd, EV_READ|EV_TIMEOUT,
              was_child_event_callback, child);

    stock_item_available(&child->base);
}

static bool
was_stock_borrow(gcc_unused void *ctx, struct stock_item *item)
{
    struct was_child *child = (struct was_child *)item;

    p_event_del(&child->event, child->base.pool);
    return true;
}

static void
was_stock_release(gcc_unused void *ctx, struct stock_item *item)
{
    struct was_child *child = (struct was_child *)item;
    static const struct timeval tv = {
        .tv_sec = 300,
        .tv_usec = 0,
    };

    p_event_add(&child->event, &tv, child->base.pool, "was_child_event");
}

static void
was_stock_destroy(gcc_unused void *ctx, struct stock_item *item)
{
    struct was_child *child =
        (struct was_child *)item;

    if (child->process.pid >= 0)
        child_kill(child->process.pid);

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
was_stock_new(struct pool *pool, unsigned limit, unsigned max_idle)
{
    return hstock_new(pool, &was_stock_class, NULL, limit, max_idle);
}

void
was_stock_get(struct hstock *hstock, struct pool *pool,
              const struct jail_params *jail,
              bool user_namespace, bool network_namespace,
              const char *executable_path,
              const char *const*args, unsigned n_args,
              const struct stock_get_handler *handler, void *handler_ctx,
              struct async_operation_ref *async_ref)
{
    GError *error = NULL;
    if (jail != NULL && !jail_params_check(jail, &error)) {
        handler->error(error, handler_ctx);
        return;
    }

    struct was_child_params *params = p_malloc(pool, sizeof(*params));
    params->executable_path = executable_path;
    params->args = args;
    params->n_args = n_args;
    params->jail = jail;
    params->user_namespace = user_namespace;
    params->network_namespace = network_namespace;

    hstock_get(hstock, pool, was_stock_key(pool, params), params,
               handler, handler_ctx, async_ref);
}

const struct was_process *
was_stock_item_get(const struct stock_item *item)
{
    const struct was_child *child = (const struct was_child *)item;

    return &child->process;
}

const char *
was_stock_translate_path(const struct stock_item *item,
                          const char *path, struct pool *pool)
{
    const struct was_child *child = (const struct was_child *)item;

    if (!child->jail_params.enabled)
        /* no JailCGI - application's namespace is the same as ours,
           no translation needed */
        return path;

    const char *jailed = jail_translate_path(&child->jail_config, path,
                                             child->jail_params.home_directory,
                                             pool);
    return jailed != NULL ? jailed : path;
}

void
was_stock_put(struct hstock *hstock, struct stock_item *item, bool destroy)
{
    struct was_child *child = (struct was_child *)item;

    hstock_put(hstock, child->key, item, destroy);
}
