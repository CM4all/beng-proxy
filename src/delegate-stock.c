/*
 * Delegate helper pooling.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "delegate-stock.h"
#include "stock.h"
#include "async.h"
#include "failure.h"
#include "fd_util.h"
#include "pevent.h"
#include "exec.h"
#include "jail.h"

#include <daemon/log.h>

#include <assert.h>
#include <errno.h>
#include <string.h>
#include <unistd.h>
#include <sys/un.h>
#include <stdlib.h>
#include <sys/socket.h>

struct delegate_info {
    const char *helper;

    const struct jail_params *jail;
};

struct delegate_process {
    struct stock_item stock_item;

    const char *uri;

    pid_t pid;
    int fd;

    struct event event;
};

static GQuark
delegate_stock_quark(void)
{
    return g_quark_from_static_string("delegate");
}

/*
 * libevent callback
 *
 */

static void
delegate_stock_event(int fd, short event, void *ctx)
{
    struct delegate_process *process = ctx;

    assert(fd == process->fd);

    p_event_consumed(&process->event, process->stock_item.pool);

    if ((event & EV_TIMEOUT) == 0) {
        char buffer;
        ssize_t nbytes;

        assert((event & EV_READ) != 0);

        nbytes = recv(fd, &buffer, sizeof(buffer), MSG_DONTWAIT);
        if (nbytes < 0)
            daemon_log(2, "error on idle delegate process: %s\n",
                       strerror(errno));
        else if (nbytes > 0)
            daemon_log(2, "unexpected data from idle delegate process\n");
    }

    stock_del(&process->stock_item);
    pool_commit();
}


/*
 * stock class
 *
 */

static struct pool *
delegate_stock_pool(void *ctx gcc_unused, struct pool *parent,
                    const char *uri gcc_unused)
{
    return pool_new_linear(parent, "delegate_stock", 512);
}

static void
delegate_stock_create(void *ctx gcc_unused, struct stock_item *item,
                      const char *uri, void *_info,
                      struct pool *caller_pool gcc_unused,
                      struct async_operation_ref *async_ref gcc_unused)
{
    struct delegate_process *process = (struct delegate_process *)item;
    int ret, fds[2];
    pid_t pid;
    const char *helper;
    const struct jail_params *jail;

    if (_info != NULL) {
        struct delegate_info *info = _info;
        helper = info->helper;
        jail = info->jail;
    } else {
        helper = uri;
        jail = NULL;
    }

    ret = socketpair_cloexec(AF_UNIX, SOCK_STREAM, 0, fds);
    if (ret < 0) {
        GError *error = g_error_new(g_file_error_quark(), errno,
                                    "socketpair() failed: %s",
                                    strerror(errno));
        stock_item_failed(item, error);
        return;
    }

    pid = fork();
    if (pid < 0) {
        GError *error = g_error_new(g_file_error_quark(), errno,
                                    "fork failed: %s", strerror(errno));
        close(fds[0]);
        close(fds[1]);
        stock_item_failed(item, error);
        return;
    } else if (pid == 0) {
        /* in the child */

        dup2(fds[1], STDIN_FILENO);
        close(fds[0]);
        close(fds[1]);

        clearenv();

        struct exec e;
        exec_init(&e);
        jail_wrapper_insert(&e, jail, NULL);
        exec_append(&e, helper);
        exec_do(&e);

        _exit(1);
    }

    /* in the parent */

    close(fds[1]);

    process->uri = uri;
    process->pid = pid;
    process->fd = fds[0];

    event_set(&process->event, process->fd, EV_READ|EV_TIMEOUT,
              delegate_stock_event, process);

    stock_item_available(&process->stock_item);
}

static bool
delegate_stock_borrow(void *ctx gcc_unused, struct stock_item *item)
{
    struct delegate_process *process =
        (struct delegate_process *)item;

    p_event_del(&process->event, process->stock_item.pool);
    return true;
}

static void
delegate_stock_release(void *ctx gcc_unused, struct stock_item *item)
{
    struct delegate_process *process =
        (struct delegate_process *)item;
    static const struct timeval tv = {
        .tv_sec = 60,
        .tv_usec = 0,
    };

    p_event_add(&process->event, &tv,
                process->stock_item.pool, "delegate_process");
}

static void
delegate_stock_destroy(void *ctx gcc_unused, struct stock_item *item)
{
    struct delegate_process *process = (struct delegate_process *)item;

    p_event_del(&process->event, process->stock_item.pool);
    close(process->fd);
}

static const struct stock_class delegate_stock_class = {
    .item_size = sizeof(struct delegate_process),
    .pool = delegate_stock_pool,
    .create = delegate_stock_create,
    .borrow = delegate_stock_borrow,
    .release = delegate_stock_release,
    .destroy = delegate_stock_destroy,
};


/*
 * interface
 *
 */

struct hstock *
delegate_stock_new(struct pool *pool)
{
    return hstock_new(pool, &delegate_stock_class, NULL, 0);
}

void
delegate_stock_get(struct hstock *delegate_stock, struct pool *pool,
                   const char *helper,
                   const struct jail_params *jail,
                   const struct stock_get_handler *handler, void *handler_ctx,
                   struct async_operation_ref *async_ref)
{
    const char *uri;
    struct delegate_info *info;

    if (jail != NULL && jail->enabled) {
        if (jail->home_directory == NULL) {
            GError *error =
                g_error_new_literal(delegate_stock_quark(), 0,
                                    "No home directory for jailed delegate");
            handler->error(error, handler_ctx);
            return;
        }

        uri = p_strcat(pool, helper, "|", jail->home_directory, "|jail", NULL);
        info = p_malloc(pool, sizeof(*info));
        info->helper = helper;
        info->jail = jail;
    } else {
        uri = helper;
        info = NULL;
    }

    hstock_get(delegate_stock, pool, uri, info,
               handler, handler_ctx, async_ref);
}

void
delegate_stock_put(struct hstock *delegate_stock,
                   struct stock_item *item, bool destroy)
{
    struct delegate_process *process = (struct delegate_process *)item;

    hstock_put(delegate_stock, process->uri, item, destroy);
}

int
delegate_stock_item_get(struct stock_item *item)
{
    struct delegate_process *process = (struct delegate_process *)item;

    assert(item != NULL);

    return process->fd;
}
