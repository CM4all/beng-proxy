/*
 * Launch and manage "Local HTTP" child processes.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "lhttp_stock.h"
#include "lhttp_quark.h"
#include "lhttp_launch.h"
#include "lhttp_address.h"
#include "stock.h"
#include "child.h"
#include "async.h"
#include "client-socket.h"
#include "pevent.h"
#include "gerrno.h"

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

struct lhttp_child {
    struct stock_item base;

    const char *key;

    struct jail_config jail_config;

    struct lhttp_process process;

    int fd;
    struct event event;

    struct async_operation create_operation;
    struct async_operation_ref connect_operation;
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

static void
lhttp_child_socket_path(struct sockaddr_un *address,
                       const char *executable_path gcc_unused)
{
    address->sun_family = AF_UNIX;

    snprintf(address->sun_path, sizeof(address->sun_path),
             "/tmp/cm4all-beng-proxy-lhttp-%u.socket",
             (unsigned)random());
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
lhttp_stock_socket_success(int fd, void *ctx)
{
    assert(fd >= 0);

    struct lhttp_child *child = ctx;
    async_ref_clear(&child->connect_operation);
    async_operation_finished(&child->create_operation);

    unlink(child->process.address.sun_path);

    child->fd = fd;

    event_set(&child->event, child->fd, EV_READ|EV_TIMEOUT,
              lhttp_child_event_callback, child);

    stock_item_available(&child->base);
}

static void
lhttp_stock_socket_timeout(void *ctx)
{
    struct lhttp_child *child = ctx;
    async_ref_clear(&child->connect_operation);
    async_operation_finished(&child->create_operation);

    unlink(child->process.address.sun_path);

    GError *error = g_error_new(errno_quark(), ETIMEDOUT,
                                "failed to connect to FastCGI server '%s': timeout",
                                child->key);
    stock_item_failed(&child->base, error);
}

static void
lhttp_stock_socket_error(GError *error, void *ctx)
{
    struct lhttp_child *child = ctx;
    async_ref_clear(&child->connect_operation);
    async_operation_finished(&child->create_operation);

    unlink(child->process.address.sun_path);

    g_prefix_error(&error, "failed to connect to FastCGI server '%s': ",
                   child->key);
    stock_item_failed(&child->base, error);
}

static const struct client_socket_handler lhttp_stock_socket_handler = {
    .success = lhttp_stock_socket_success,
    .timeout = lhttp_stock_socket_timeout,
    .error = lhttp_stock_socket_error,
};

/*
 * async operation
 *
 */

static struct lhttp_child *
async_to_lhttp_child(struct async_operation *ao)
{
    return (struct lhttp_child*)(((char*)ao) - offsetof(struct lhttp_child, create_operation));
}

static void
lhttp_create_abort(struct async_operation *ao)
{
    struct lhttp_child *child = async_to_lhttp_child(ao);

    assert(child != NULL);
    assert(async_ref_defined(&child->connect_operation));

    unlink(child->process.address.sun_path);

    if (child->process.pid >= 0)
        child_kill(child->process.pid);

    async_abort(&child->connect_operation);
    stock_item_aborted(&child->base);
}

static const struct async_operation_class lhttp_create_operation = {
    .abort = lhttp_create_abort,
};

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
                  struct pool *caller_pool,
                  struct async_operation_ref *async_ref)
{
    struct pool *pool = item->pool;
    const struct lhttp_address *address = info;
    struct lhttp_child *child = (struct lhttp_child *)item;

    assert(key != NULL);
    assert(address != NULL);
    assert(address->path != NULL);

    child->key = p_strdup(pool, key);
    lhttp_child_socket_path(&child->process.address, key);

    if (address->jail.enabled) {
        if (!jail_config_load(&child->jail_config,
                              "/etc/cm4all/jailcgi/jail.conf", pool)) {
            GError *error = g_error_new(lhttp_quark(), 0,
                                        "Failed to load /etc/cm4all/jailcgi/jail.conf");
            stock_item_failed(item, error);
            return;
        }
    }

    GError *error = NULL;
    if (!lhttp_launch(&child->process, address, &error)) {
        stock_item_failed(item, error);
        return;
    }

    child_register(child->process.pid, key, lhttp_child_callback, child);

    child->fd = -1;

    async_init(&child->create_operation, &lhttp_create_operation);
    async_ref_set(async_ref, &child->create_operation);

    client_socket_new(caller_pool, AF_UNIX, SOCK_STREAM, 0,
                      (const struct sockaddr*)&child->process.address,
                      SUN_LEN(&child->process.address),
                      10,
                      &lhttp_stock_socket_handler, child,
                      &child->connect_operation);
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

    if (async_ref_defined(&child->connect_operation))
        async_abort(&child->connect_operation);
    else if (child->fd >= 0) {
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

void
lhttp_stock_get(struct hstock *hstock, struct pool *pool,
                const struct lhttp_address *address,
                const struct stock_get_handler *handler, void *handler_ctx,
                struct async_operation_ref *async_ref)
{
    if (address->jail.enabled && address->jail.home_directory == NULL) {
        GError *error =
            g_error_new_literal(lhttp_quark(), 0,
                                "No home directory for jailed LHTTP");
        handler->error(error, handler_ctx);
        return;
    }

    union {
        const struct lhttp_address *in;
        void *out;
    } deconst = { .in = address };

    hstock_get(hstock, pool, lhttp_stock_key(pool, address), deconst.out,
               handler, handler_ctx, async_ref);
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
