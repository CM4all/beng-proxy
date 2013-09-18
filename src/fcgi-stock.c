/*
 * Launch and manage FastCGI child processes.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "fcgi-stock.h"
#include "fcgi-quark.h"
#include "fcgi-launch.h"
#include "stock.h"
#include "child.h"
#include "async.h"
#include "client-socket.h"
#include "jail.h"
#include "pevent.h"
#include "gerrno.h"
#include "child_socket.h"

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

struct fcgi_child_params {
    const char *executable_path;

    const struct jail_params *jail;
};

struct fcgi_child {
    struct stock_item base;

    const char *key;

    struct jail_params jail_params;

    struct jail_config jail_config;

    struct child_socket socket;

    pid_t pid;

    int fd;
    struct event event;

    struct async_operation create_operation;
    struct async_operation_ref connect_operation;
};

static const char *
fcgi_stock_key(struct pool *pool, const struct fcgi_child_params *params)
{
    return params->jail == NULL || !params->jail->enabled
        ? params->executable_path
        : p_strcat(pool, params->executable_path, "|",
                   params->jail->home_directory, NULL);
}

static void
fcgi_child_callback(int status gcc_unused, void *ctx)
{
    struct fcgi_child *child = ctx;

    child->pid = -1;
}

/*
 * libevent callback
 *
 */

static void
fcgi_child_event_callback(int fd, G_GNUC_UNUSED short event, void *ctx)
{
    struct fcgi_child *child = ctx;

    assert(fd == child->fd);

    p_event_consumed(&child->event, child->base.pool);

    if ((event & EV_TIMEOUT) == 0) {
        char buffer;
        ssize_t nbytes = recv(fd, &buffer, sizeof(buffer), MSG_DONTWAIT);
        if (nbytes < 0)
            daemon_log(2, "error on idle FastCGI connection: %s\n",
                       strerror(errno));
        else if (nbytes > 0)
            daemon_log(2, "unexpected data from idle FastCGI connection\n");
    }

    stock_del(&child->base);
    pool_commit();
}

/*
 * client_socket handler
 *
 */

static void
fcgi_stock_socket_success(int fd, void *ctx)
{
    assert(fd >= 0);

    struct fcgi_child *child = ctx;
    async_ref_clear(&child->connect_operation);
    async_operation_finished(&child->create_operation);

    child_socket_unlink(&child->socket);

    child->fd = fd;

    event_set(&child->event, child->fd, EV_READ|EV_TIMEOUT,
              fcgi_child_event_callback, child);

    stock_item_available(&child->base);
}

static void
fcgi_stock_socket_timeout(void *ctx)
{
    struct fcgi_child *child = ctx;
    async_ref_clear(&child->connect_operation);
    async_operation_finished(&child->create_operation);

    child_socket_unlink(&child->socket);

    GError *error = g_error_new(errno_quark(), ETIMEDOUT,
                                "failed to connect to FastCGI server '%s': timeout",
                                child->key);
    stock_item_failed(&child->base, error);
}

static void
fcgi_stock_socket_error(GError *error, void *ctx)
{
    struct fcgi_child *child = ctx;
    async_ref_clear(&child->connect_operation);
    async_operation_finished(&child->create_operation);

    child_socket_unlink(&child->socket);

    g_prefix_error(&error, "failed to connect to FastCGI server '%s': ",
                   child->key);
    stock_item_failed(&child->base, error);
}

static const struct client_socket_handler fcgi_stock_socket_handler = {
    .success = fcgi_stock_socket_success,
    .timeout = fcgi_stock_socket_timeout,
    .error = fcgi_stock_socket_error,
};

/*
 * async operation
 *
 */

static struct fcgi_child *
async_to_fcgi_child(struct async_operation *ao)
{
    return (struct fcgi_child*)(((char*)ao) - offsetof(struct fcgi_child, create_operation));
}

static void
fcgi_create_abort(struct async_operation *ao)
{
    struct fcgi_child *child = async_to_fcgi_child(ao);

    assert(child != NULL);
    assert(async_ref_defined(&child->connect_operation));

    child_socket_unlink(&child->socket);

    if (child->pid >= 0)
        child_kill(child->pid);

    async_abort(&child->connect_operation);
    stock_item_aborted(&child->base);
}

static const struct async_operation_class fcgi_create_operation = {
    .abort = fcgi_create_abort,
};

/*
 * stock class
 *
 */

static struct pool *
fcgi_stock_pool(void *ctx gcc_unused, struct pool *parent,
               const char *uri gcc_unused)
{
    return pool_new_linear(parent, "fcgi_child", 2048);
}

static void
fcgi_stock_create(G_GNUC_UNUSED void *ctx, struct stock_item *item,
                  const char *key, void *info,
                  struct pool *caller_pool,
                  struct async_operation_ref *async_ref)
{
    struct pool *pool = item->pool;
    struct fcgi_child_params *params = info;
    struct fcgi_child *child = (struct fcgi_child *)item;

    assert(key != NULL);
    assert(params != NULL);
    assert(params->executable_path != NULL);

    child->key = p_strdup(pool, key);

    if (params->jail != NULL && params->jail->enabled) {
        jail_params_copy(pool, &child->jail_params, params->jail);

        if (!jail_config_load(&child->jail_config,
                              "/etc/cm4all/jailcgi/jail.conf", pool)) {
            GError *error = g_error_new(fcgi_quark(), 0,
                                        "Failed to load /etc/cm4all/jailcgi/jail.conf");
            stock_item_failed(item, error);
            return;
        }
    } else
        child->jail_params.enabled = false;

    GError *error = NULL;
    int fd = child_socket_create(&child->socket, &error);
    if (fd < 0) {
        stock_item_failed(item, error);
        return;
    }

    child->pid = fcgi_spawn_child(params->jail,
                                  params->executable_path,
                                  fd, &error);
    close(fd);
    if (child->pid < 0) {
        child_socket_unlink(&child->socket);
        stock_item_failed(item, error);
        return;
    }

    child_register(child->pid, key, fcgi_child_callback, child);

    child->fd = -1;

    async_init(&child->create_operation, &fcgi_create_operation);
    async_ref_set(async_ref, &child->create_operation);

    client_socket_new(caller_pool, AF_UNIX, SOCK_STREAM, 0,
                      child_socket_address(&child->socket),
                      child_socket_address_length(&child->socket),
                      10,
                      &fcgi_stock_socket_handler, child,
                      &child->connect_operation);
}

static bool
fcgi_stock_borrow(void *ctx gcc_unused, struct stock_item *item)
{
    struct fcgi_child *child = (struct fcgi_child *)item;

    p_event_del(&child->event, child->base.pool);
    return true;
}

static void
fcgi_stock_release(void *ctx gcc_unused, struct stock_item *item)
{
    struct fcgi_child *child = (struct fcgi_child *)item;
    static const struct timeval tv = {
        .tv_sec = 300,
        .tv_usec = 0,
    };

    p_event_add(&child->event, &tv, child->base.pool, "fcgi_child_event");
}

static void
fcgi_stock_destroy(void *ctx gcc_unused, struct stock_item *item)
{
    struct fcgi_child *child =
        (struct fcgi_child *)item;

    if (child->pid >= 0)
        child_kill(child->pid);

    if (async_ref_defined(&child->connect_operation))
        async_abort(&child->connect_operation);
    else if (child->fd >= 0) {
        p_event_del(&child->event, child->base.pool);
        close(child->fd);
    }
}

static const struct stock_class fcgi_stock_class = {
    .item_size = sizeof(struct fcgi_child),
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

struct hstock *
fcgi_stock_new(struct pool *pool, unsigned limit, unsigned max_idle)
{
    return hstock_new(pool, &fcgi_stock_class, NULL, limit, max_idle);
}

void
fcgi_stock_get(struct hstock *hstock, struct pool *pool,
               const struct jail_params *jail,
               const char *executable_path,
               const struct stock_get_handler *handler, void *handler_ctx,
               struct async_operation_ref *async_ref)
{
    GError *error = NULL;
    if (jail != NULL && !jail_params_check(jail, &error)) {
        handler->error(error, handler_ctx);
        return;
    }

    struct fcgi_child_params *params = p_malloc(pool, sizeof(*params));
    params->executable_path = executable_path;
    params->jail = jail;

    hstock_get(hstock, pool, fcgi_stock_key(pool, params), params,
               handler, handler_ctx, async_ref);
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
    const struct fcgi_child *child = (const struct fcgi_child *)item;

    assert(child->fd >= 0);

    return child->fd;
}

const char *
fcgi_stock_translate_path(const struct stock_item *item,
                          const char *path, struct pool *pool)
{
    const struct fcgi_child *child = (const struct fcgi_child *)item;

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
fcgi_stock_put(struct hstock *hstock, struct stock_item *item, bool destroy)
{
    struct fcgi_child *child = (struct fcgi_child *)item;

    hstock_put(hstock, child->key, item, destroy);
}
