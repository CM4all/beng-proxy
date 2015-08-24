/*
 * Launch and manage WAS child processes.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "was_stock.hxx"
#include "was_quark.h"
#include "was_launch.hxx"
#include "hstock.hxx"
#include "stock.hxx"
#include "child_manager.hxx"
#include "async.hxx"
#include "net/ConnectSocket.hxx"
#include "ChildOptions.hxx"
#include "pevent.hxx"
#include "pool.hxx"
#include "JailConfig.hxx"
#include "util/ConstBuffer.hxx"
#include "util/Cast.hxx"

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

struct WasChildParams {
    const char *executable_path;

    ConstBuffer<const char *> args;
    ConstBuffer<const char *> env;

    const ChildOptions *options;
};

struct WasChild {
    StockItem base;

    const char *key;

    JailParams jail_params;

    struct jail_config jail_config;

    struct was_process process;
    struct event event;
};

static const char *
was_stock_key(struct pool *pool, const WasChildParams *params)
{
    const char *key = params->executable_path;
    for (auto i : params->args)
        key = p_strcat(pool, key, " ", i, nullptr);

    for (auto i : params->env)
        key = p_strcat(pool, key, "$", i, nullptr);

    char options_buffer[4096];
    *params->options->MakeId(options_buffer) = 0;
    if (*options_buffer != 0)
        key = p_strcat(pool, key, options_buffer, nullptr);

    return key;
}

static void
was_child_callback(gcc_unused int status, void *ctx)
{
    WasChild *child = (WasChild *)ctx;

    child->process.pid = -1;
}

/*
 * libevent callback
 *
 */

static void
was_child_event_callback(int fd, gcc_unused short event, void *ctx)
{
    WasChild *child = (WasChild *)ctx;

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

    stock_del(child->base);
    pool_commit();
}

/*
 * stock class
 *
 */

static constexpr WasChild &
ToWasChild(StockItem &item)
{
    return ContainerCast2(item, &WasChild::base);
}

static constexpr const WasChild &
ToWasChild(const StockItem &item)
{
    return ContainerCast2(item, &WasChild::base);
}

static struct pool *
was_stock_pool(gcc_unused void *ctx, struct pool &parent,
               gcc_unused const char *uri)
{
    return pool_new_linear(&parent, "was_child", 2048);
}

static void
was_stock_create(gcc_unused void *ctx, StockItem &item,
                 const char *key, void *info,
                 struct pool &caller_pool,
                 struct async_operation_ref &async_ref)
{
    struct pool *pool = item.pool;
    WasChildParams *params = (WasChildParams *)info;
    auto *child = &ToWasChild(item);

    (void)caller_pool;
    (void)async_ref;

    assert(key != nullptr);
    assert(params != nullptr);
    assert(params->executable_path != nullptr);

    child->key = p_strdup(pool, key);

    const ChildOptions &options = *params->options;
    if (options.jail.enabled) {
        child->jail_params.CopyFrom(*pool, options.jail);

        if (!jail_config_load(&child->jail_config,
                              "/etc/cm4all/jailcgi/jail.conf", pool)) {
            GError *error = g_error_new(was_quark(), 0,
                                        "Failed to load /etc/cm4all/jailcgi/jail.conf");
            stock_item_failed(item, error);
            return;
        }
    } else
        child->jail_params.enabled = false;

    GError *error = nullptr;
    if (!was_launch(&child->process, params->executable_path,
                    params->args, params->env,
                    options,
                    &error)) {
        stock_item_failed(item, error);
        return;
    }

    child_register(child->process.pid, key, was_child_callback, child);

    event_set(&child->event, child->process.control_fd, EV_READ|EV_TIMEOUT,
              was_child_event_callback, child);

    stock_item_available(child->base);
}

static bool
was_stock_borrow(gcc_unused void *ctx, StockItem &item)
{
    auto *child = &ToWasChild(item);

    p_event_del(&child->event, child->base.pool);
    return true;
}

static void
was_stock_release(gcc_unused void *ctx, StockItem &item)
{
    auto *child = &ToWasChild(item);
    static const struct timeval tv = {
        .tv_sec = 300,
        .tv_usec = 0,
    };

    p_event_add(&child->event, &tv, child->base.pool, "was_child_event");
}

static void
was_stock_destroy(gcc_unused void *ctx, StockItem &item)
{
    auto *child = &ToWasChild(item);

    if (child->process.pid >= 0)
        child_kill(child->process.pid);

    if (child->process.control_fd >= 0) {
        p_event_del(&child->event, child->base.pool);
        close(child->process.control_fd);
    }

    close(child->process.input_fd);
    close(child->process.output_fd);
}

static constexpr StockClass was_stock_class = {
    .item_size = sizeof(WasChild),
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

StockMap *
was_stock_new(struct pool *pool, unsigned limit, unsigned max_idle)
{
    return hstock_new(*pool, was_stock_class, nullptr, limit, max_idle);
}

void
was_stock_get(StockMap *hstock, struct pool *pool,
              const ChildOptions &options,
              const char *executable_path,
              ConstBuffer<const char *> args,
              ConstBuffer<const char *> env,
              const StockGetHandler *handler, void *handler_ctx,
              struct async_operation_ref *async_ref)
{
    auto params = NewFromPool<WasChildParams>(*pool);
    params->executable_path = executable_path;
    params->args = args;
    params->env = env;
    params->options = &options;

    hstock_get(*hstock, *pool, was_stock_key(pool, params), params,
               *handler, handler_ctx, *async_ref);
}

const struct was_process &
was_stock_item_get(const StockItem &item)
{
    auto *child = &ToWasChild(item);

    return child->process;
}

const char *
was_stock_translate_path(const StockItem *item,
                         const char *path, struct pool *pool)
{
    auto *child = &ToWasChild(*item);

    if (!child->jail_params.enabled)
        /* no JailCGI - application's namespace is the same as ours,
           no translation needed */
        return path;

    const char *jailed = jail_translate_path(&child->jail_config, path,
                                             child->jail_params.home_directory,
                                             pool);
    return jailed != nullptr ? jailed : path;
}

void
was_stock_put(StockMap *hstock, StockItem &item, bool destroy)
{
    auto &child = ToWasChild(item);

    hstock_put(*hstock, child.key, item, destroy);
}
