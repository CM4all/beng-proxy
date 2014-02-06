/*
 * Launch and manage FastCGI child processes.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "fcgi_stock.h"
#include "fcgi_quark.h"
#include "fcgi_launch.h"
#include "child_stock.h"
#include "hstock.h"
#include "stock.h"
#include "child_manager.h"
#include "child_options.h"
#include "pevent.h"
#include "gerrno.h"

#include <daemon/log.h>

#include <glib.h>

#include <assert.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>

#ifdef __linux
#include <sched.h>
#endif

struct fcgi_stock {
    struct hstock *hstock;
    struct hstock *child_stock;
};

struct fcgi_child_params {
    const char *executable_path;

    const char *const*args;
    unsigned n_args;

    const struct child_options *options;
};

struct fcgi_connection {
    struct stock_item base;

    struct jail_params jail_params;

    struct jail_config jail_config;

    struct stock_item *child;

    int fd;
    struct event event;

    /**
     * Is this a fresh connection to the FastCGI child process?
     */
    bool fresh;

    /**
     * Shall the FastCGI child process be killed?
     */
    bool kill;

    /**
     * Was the current request aborted by the fcgi_client caller?
     */
    bool aborted;
};

static const char *
fcgi_stock_key(struct pool *pool, const struct fcgi_child_params *params)
{
    const char *key = params->executable_path;
    for (unsigned i = 0, n = params->n_args; i < n; ++i)
        key = p_strcat(pool, key, " ", params->args[i], NULL);

    char options_buffer[4096];
    *child_options_id(params->options, options_buffer) = 0;
    if (*options_buffer != 0)
        key = p_strcat(pool, key, options_buffer, NULL);

    return key;
}

/*
 * libevent callback
 *
 */

static void
fcgi_connection_event_callback(int fd, G_GNUC_UNUSED short event, void *ctx)
{
    struct fcgi_connection *connection = ctx;

    assert(fd == connection->fd);

    p_event_consumed(&connection->event, connection->base.pool);

    if ((event & EV_TIMEOUT) == 0) {
        char buffer;
        ssize_t nbytes = recv(fd, &buffer, sizeof(buffer), MSG_DONTWAIT);
        if (nbytes < 0)
            daemon_log(2, "error on idle FastCGI connection: %s\n",
                       strerror(errno));
        else if (nbytes > 0)
            daemon_log(2, "unexpected data from idle FastCGI connection\n");
    }

    stock_del(&connection->base);
    pool_commit();
}

/*
 * child_stock class
 *
 */

static int
fcgi_child_stock_clone_flags(gcc_unused const char *key, void *info, int flags,
                             gcc_unused void *ctx)
{
    const struct fcgi_child_params *params = info;
    const struct child_options *const options = params->options;

    return namespace_options_clone_flags(&options->ns, flags);
}

static int
fcgi_child_stock_run(gcc_unused struct pool *pool, gcc_unused const char *key,
                     void *info, gcc_unused void *ctx)
{
    const struct fcgi_child_params *params = info;
    const struct child_options *const options = params->options;

    rlimit_options_apply(&options->rlimits);
    namespace_options_setup(&options->ns);

    fcgi_run(&options->jail, params->executable_path,
             params->args, params->n_args);
}

static const struct child_stock_class fcgi_child_stock_class = {
    .shutdown_signal = SIGUSR1,
    .clone_flags = fcgi_child_stock_clone_flags,
    .run = fcgi_child_stock_run,
};

/*
 * stock class
 *
 */

static struct pool *
fcgi_stock_pool(void *ctx gcc_unused, struct pool *parent,
               const char *uri gcc_unused)
{
    return pool_new_linear(parent, "fcgi_connection", 2048);
}

static void
fcgi_stock_create(void *ctx, struct stock_item *item,
                  const char *key, void *info,
                  gcc_unused struct pool *caller_pool,
                  gcc_unused struct async_operation_ref *async_ref)
{
    struct fcgi_stock *fcgi_stock = ctx;
    struct pool *pool = item->pool;
    struct fcgi_child_params *params = info;
    struct fcgi_connection *connection = (struct fcgi_connection *)item;

    assert(key != NULL);
    assert(params != NULL);
    assert(params->executable_path != NULL);

    const struct child_options *const options = params->options;
    if (options->jail.enabled) {
        jail_params_copy(pool, &connection->jail_params, &options->jail);

        if (!jail_config_load(&connection->jail_config,
                              "/etc/cm4all/jailcgi/jail.conf", pool)) {
            GError *error = g_error_new(fcgi_quark(), 0,
                                        "Failed to load /etc/cm4all/jailcgi/jail.conf");
            stock_item_failed(item, error);
            return;
        }
    } else
        connection->jail_params.enabled = false;

    GError *error = NULL;
    connection->child = hstock_get_now(fcgi_stock->child_stock, pool,
                                       key, params, &error);
    if (connection->child == NULL) {
        g_prefix_error(&error, "failed to start to FastCGI server '%s': ",
                       key);

        stock_item_failed(item, error);
        return;
    }

    connection->fd = child_stock_item_connect(connection->child, &error);
    if (connection->fd < 0) {
        g_prefix_error(&error, "failed to connect to FastCGI server '%s': ",
                       key);

        child_stock_put(fcgi_stock->child_stock, connection->child, true);
        stock_item_failed(item, error);
        return;
    }

    connection->fresh = true;
    connection->kill = false;

    event_set(&connection->event, connection->fd, EV_READ|EV_TIMEOUT,
              fcgi_connection_event_callback, connection);

    stock_item_available(&connection->base);
}

static bool
fcgi_stock_borrow(void *ctx gcc_unused, struct stock_item *item)
{
    struct fcgi_connection *connection = (struct fcgi_connection *)item;

    p_event_del(&connection->event, connection->base.pool);
    connection->aborted = false;
    return true;
}

static void
fcgi_stock_release(void *ctx gcc_unused, struct stock_item *item)
{
    struct fcgi_connection *connection = (struct fcgi_connection *)item;
    static const struct timeval tv = {
        .tv_sec = 300,
        .tv_usec = 0,
    };

    connection->fresh = false;

    p_event_add(&connection->event, &tv, connection->base.pool,
                "fcgi_connection_event");
}

static void
fcgi_stock_destroy(void *ctx, struct stock_item *item)
{
    struct fcgi_stock *fcgi_stock = ctx;
    struct fcgi_connection *connection =
        (struct fcgi_connection *)item;

    p_event_del(&connection->event, connection->base.pool);
    close(connection->fd);

    child_stock_put(fcgi_stock->child_stock, connection->child,
                    connection->kill);
}

static const struct stock_class fcgi_stock_class = {
    .item_size = sizeof(struct fcgi_connection),
    .pool = fcgi_stock_pool,
    .create = fcgi_stock_create,
    .borrow = fcgi_stock_borrow,
    .release = fcgi_stock_release,
    .destroy = fcgi_stock_destroy,
};


/*
 * interface
 *
 */

struct fcgi_stock *
fcgi_stock_new(struct pool *pool, unsigned limit, unsigned max_idle)
{
    struct fcgi_stock *fcgi_stock = p_malloc(pool, sizeof(*fcgi_stock));
    fcgi_stock->child_stock = child_stock_new(pool, limit, max_idle,
                                              &fcgi_child_stock_class);
    fcgi_stock->hstock = hstock_new(pool, &fcgi_stock_class, fcgi_stock,
                                    limit, max_idle);

    return fcgi_stock;
}

void
fcgi_stock_free(struct fcgi_stock *fcgi_stock)
{
    hstock_free(fcgi_stock->hstock);
    hstock_free(fcgi_stock->child_stock);
}

struct stock_item *
fcgi_stock_get(struct fcgi_stock *fcgi_stock, struct pool *pool,
               const struct child_options *options,
               const char *executable_path,
               const char *const*args, unsigned n_args,
               GError **error_r)
{
    if (!jail_params_check(&options->jail, error_r))
        return NULL;

    struct fcgi_child_params *params = p_malloc(pool, sizeof(*params));
    params->executable_path = executable_path;
    params->args = args;
    params->n_args = n_args;
    params->options = options;

    return hstock_get_now(fcgi_stock->hstock, pool,
                          fcgi_stock_key(pool, params), params,
                          error_r);
}

int
fcgi_stock_item_get_domain(const struct stock_item *item)
{
    (void)item;

    return AF_UNIX;
}

int
fcgi_stock_item_get(const struct stock_item *item)
{
    const struct fcgi_connection *connection =
        (const struct fcgi_connection *)item;

    assert(connection->fd >= 0);

    return connection->fd;
}

const char *
fcgi_stock_translate_path(const struct stock_item *item,
                          const char *path, struct pool *pool)
{
    const struct fcgi_connection *connection =
        (const struct fcgi_connection *)item;

    if (!connection->jail_params.enabled)
        /* no JailCGI - application's namespace is the same as ours,
           no translation needed */
        return path;

    const char *jailed = jail_translate_path(&connection->jail_config, path,
                                             connection->jail_params.home_directory,
                                             pool);
    return jailed != NULL ? jailed : path;
}

void
fcgi_stock_put(struct fcgi_stock *fcgi_stock, struct stock_item *item,
               bool destroy)
{
    struct fcgi_connection *connection = (struct fcgi_connection *)item;

    if (connection->fresh && connection->aborted && destroy)
        /* the fcgi_client caller has aborted the request before the
           first response on a fresh connection was received: better
           kill the child process, it may be failing on us
           completely */
        connection->kill = true;

    hstock_put(fcgi_stock->hstock, child_stock_item_key(connection->child),
               item, destroy);
}

void
fcgi_stock_aborted(struct stock_item *item)
{
    struct fcgi_connection *connection = (struct fcgi_connection *)item;

    connection->aborted = true;
}
