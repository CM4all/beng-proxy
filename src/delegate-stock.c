/*
 * Delegate helper pooling.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "delegate-stock.h"
#include "stock.h"
#include "async.h"
#include "failure.h"
#include "fd-util.h"

#include <daemon/log.h>

#include <assert.h>
#include <errno.h>
#include <string.h>
#include <unistd.h>
#include <sys/un.h>
#include <event.h>

struct delegate_process {
    struct stock_item stock_item;

    pid_t pid;
    int fd;

    struct event event;
};


/*
 * libevent callback
 *
 */

static void
delegate_stock_event(int fd, short event, void *ctx)
{
    struct delegate_process *process = ctx;

    assert(fd == process->fd);

    if ((event & EV_TIMEOUT) == 0) {
        char buffer;
        ssize_t nbytes;

        assert((event & EV_READ) != 0);

        nbytes = read(fd, &buffer, sizeof(buffer));
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

static pool_t
delegate_stock_pool(void *ctx __attr_unused, pool_t parent,
                    const char *uri __attr_unused)
{
    return pool_new_linear(parent, "delegate_stock", 512);
}

static void
delegate_stock_create(void *ctx __attr_unused, struct stock_item *item,
                      const char *uri, void *info __attr_unused,
                      struct async_operation_ref *async_ref __attr_unused)
{
    struct delegate_process *process = (struct delegate_process *)item;
    int ret, fds[2];
    pid_t pid;

    ret = socketpair(AF_UNIX, SOCK_STREAM, 0, fds);
    if (ret < 0) {
        daemon_log(1, "socketpair() failed: %s\n", strerror(errno));
        stock_item_failed(item);
        return;
    }

    pid = fork();
    if (pid < 0) {
        daemon_log(1, "fork() failed: %s\n", strerror(errno));
        close(fds[0]);
        close(fds[1]);
        stock_item_failed(item);
        return;
    } else if (pid == 0) {
        /* in the child */

        dup2(fds[1], STDIN_FILENO);
        close(fds[0]);
        close(fds[1]);

        execl(uri, uri, NULL);
        _exit(1);
    }

    /* in the parent */

    close(fds[1]);

    fd_set_cloexec(fds[0]);

    process->pid = pid;
    process->fd = fds[0];

    event_set(&process->event, process->fd, EV_READ|EV_TIMEOUT,
              delegate_stock_event, process);

    stock_item_available(&process->stock_item);
}

static bool
delegate_stock_borrow(void *ctx __attr_unused, struct stock_item *item)
{
    struct delegate_process *process =
        (struct delegate_process *)item;

    event_del(&process->event);
    return true;
}

static void
delegate_stock_release(void *ctx __attr_unused, struct stock_item *item)
{
    struct delegate_process *process =
        (struct delegate_process *)item;
    struct timeval tv = {
        .tv_sec = 60,
        .tv_usec = 0,
    };

    event_add(&process->event, &tv);
}

static void
delegate_stock_destroy(void *ctx __attr_unused, struct stock_item *item)
{
    struct delegate_process *process = (struct delegate_process *)item;

    if (process->fd >= 0) {
        event_del(&process->event);
        close(process->fd);
    }
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
delegate_stock_new(pool_t pool)
{
    return hstock_new(pool, &delegate_stock_class, NULL);
}

int
delegate_stock_item_get(struct stock_item *item)
{
    struct delegate_process *process = (struct delegate_process *)item;

    assert(item != NULL);

    return process->fd;
}
