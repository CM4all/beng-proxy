/*
 * Launch and manage FastCGI child processes.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "fcgi-stock.h"
#include "child.h"
#include "async.h"
#include "client-socket.h"

#include <daemon/log.h>

#include <glib.h>
#include <event.h>
#include <assert.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <signal.h>

struct fcgi_child {
    struct stock_item base;

    const char *executable_path;

    struct sockaddr_un address;

    pid_t pid;

    int fd;
    struct event event;

    struct async_operation create_operation;
    struct async_operation_ref connect_operation;
};

static void
fcgi_child_callback(int status __attr_unused, void *ctx)
{
    struct fcgi_child *child = ctx;

    child->pid = -1;
}

static void
fcgi_child_socket_path(struct sockaddr_un *address,
                       const char *executable_path __attr_unused)
{
    address->sun_family = AF_UNIX;

    snprintf(address->sun_path, sizeof(address->sun_path),
             "/tmp/cm4all-beng-proxy-fcgi-%u.socket",
             (unsigned)random());
}

static int
fcgi_create_socket(const struct fcgi_child *child)
{
    int ret = unlink(child->address.sun_path);
    if (ret != 0 && errno != ENOENT) {
        daemon_log(2, "failed to unlink %s: %s\n",
                   child->address.sun_path, strerror(errno));
        return -1;
    }

    int fd = socket(PF_UNIX, SOCK_STREAM, 0);
    if (fd < 0) {
        daemon_log(2, "failed to create unix socket %s: %s\n",
                   child->address.sun_path, strerror(errno));
        return -1;
    }

    ret = bind(fd, (const struct sockaddr*)&child->address,
               sizeof(child->address));
    if (ret < 0) {
        daemon_log(2, "bind(%s) failed: %s\n",
                   child->address.sun_path, strerror(errno));
        close(fd);
        return -1;
    }

    ret = listen(fd, 8);
    if (ret < 0) {
        daemon_log(2, "listen() failed: %s\n", strerror(errno));
        close(fd);
        return -1;
    }

    return fd;
}

static pid_t
fcgi_spawn_child(const char *executable_path, int fd)
{
    pid_t pid = fork();
    if (pid < 0) {
        daemon_log(2, "fork() failed: %s\n", strerror(errno));
        return -1;
    }

    if (pid == 0) {
        dup2(fd, 0);
        close(fd);

        fd = open("/dev/null", O_WRONLY);
        if (fd >= 0) {
            dup2(fd, 1);
            dup2(fd, 2);
        } else {
            close(1);
            close(2);
        }

        execl(executable_path, executable_path, NULL);
        daemon_log(1, "failed to execute %s: %s\n",
                   executable_path, strerror(errno));
        _exit(1);
    }

    return pid;
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

    char buffer;
    ssize_t nbytes = recv(fd, &buffer, sizeof(buffer), MSG_DONTWAIT);
    if (nbytes < 0)
        daemon_log(2, "error on idle FastCGI connection: %s\n",
                   strerror(errno));
    else if (nbytes > 0)
        daemon_log(2, "unexpected data from idle FastCGI connection\n");

    stock_del(&child->base);
    pool_commit();
}

/*
 * client_socket callback
 *
 */

static void
fcgi_connect_callback(int fd, int err, void *ctx)
{
    struct fcgi_child *child = ctx;

    unlink(child->address.sun_path);

    async_ref_clear(&child->connect_operation);

    if (err == 0) {
        assert(fd >= 0);

        child->fd = fd;

        event_set(&child->event, child->fd, EV_READ,
                  fcgi_child_event_callback, child);

        stock_item_available(&child->base);
    } else {
        daemon_log(1, "failed to connect to FastCGI server '%s': %s\n",
                   child->executable_path, strerror(err));

        stock_item_failed(&child->base);
    }
}

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

    unlink(child->address.sun_path);

    if (child->pid >= 0) {
        kill(child->pid, SIGTERM);
        child_clear(child->pid);
    }

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

static pool_t
fcgi_stock_pool(void *ctx __attr_unused, pool_t parent,
               const char *uri __attr_unused)
{
    return pool_new_linear(parent, "fcgi_child", 2048);
}

static void
fcgi_stock_create(G_GNUC_UNUSED void *ctx, struct stock_item *item,
                  const char *executable_path, G_GNUC_UNUSED void *info,
                  pool_t caller_pool,
                  struct async_operation_ref *async_ref)
{
    pool_t pool = item->pool;
    struct fcgi_child *child = (struct fcgi_child *)item;

    assert(executable_path != NULL);

    child->executable_path = p_strdup(pool, executable_path);
    fcgi_child_socket_path(&child->address, executable_path);

    int fd = fcgi_create_socket(child);
    if (fd < 0) {
        stock_item_failed(item);
        return;
    }

    child->pid = fcgi_spawn_child(executable_path, fd);
    close(fd);
    if (child->pid < 0) {
        stock_item_failed(item);
        return;
    }

    child_register(child->pid, fcgi_child_callback, child);

    child->fd = -1;

    async_init(&child->create_operation, &fcgi_create_operation);
    async_ref_set(async_ref, &child->create_operation);

    client_socket_new(caller_pool, AF_UNIX, SOCK_STREAM, 0,
                      (const struct sockaddr*)&child->address,
                      sizeof(child->address),
                      fcgi_connect_callback, child,
                      &child->connect_operation);
}

static bool
fcgi_stock_borrow(void *ctx __attr_unused, struct stock_item *item)
{
    struct fcgi_child *child = (struct fcgi_child *)item;

    event_del(&child->event);
    return true;
}

static void
fcgi_stock_release(void *ctx __attr_unused, struct stock_item *item)
{
    struct fcgi_child *child = (struct fcgi_child *)item;
    struct timeval tv = {
        .tv_sec = 60,
        .tv_usec = 0,
    };

    event_add(&child->event, &tv);
}

static void
fcgi_stock_destroy(void *ctx __attr_unused, struct stock_item *item)
{
    struct fcgi_child *child =
        (struct fcgi_child *)item;

    if (child->pid >= 0) {
        kill(child->pid, SIGTERM);
        child_clear(child->pid);
    }

    if (async_ref_defined(&child->connect_operation))
        async_abort(&child->connect_operation);
    else if (child->fd >= 0) {
        event_del(&child->event);
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
fcgi_stock_new(pool_t pool, unsigned limit)
{
    return hstock_new(pool, &fcgi_stock_class, NULL, limit);
}

void
fcgi_stock_get(struct hstock *hstock, pool_t pool,
               const char *executable_path,
               stock_callback_t callback, void *callback_ctx,
               struct async_operation_ref *async_ref)
{
    hstock_get(hstock, pool, executable_path, NULL,
               callback, callback_ctx, async_ref);
}

int
fcgi_stock_item_get(const struct stock_item *item)
{
    const struct fcgi_child *child = (const struct fcgi_child *)item;

    assert(child->fd >= 0);

    return child->fd;
}

