/*
 * Delegate helper pooling.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "delegate_stock.hxx"
#include "hstock.h"
#include "async.h"
#include "failure.hxx"
#include "fd_util.h"
#include "pevent.h"
#include "exec.hxx"
#include "child_options.hxx"
#include "gerrno.h"
#include "sigutil.h"

#include <daemon/log.h>

#include <assert.h>
#include <unistd.h>
#include <sched.h>
#include <sys/un.h>
#include <stdlib.h>
#include <sys/socket.h>

struct delegate_info {
    const char *helper;

    const struct child_options *options;

    int fds[2];
    sigset_t signals;
};

struct delegate_process {
    struct stock_item stock_item;

    const char *uri;

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
    struct delegate_process *process = (struct delegate_process *)ctx;

    assert(fd == process->fd);

    p_event_consumed(&process->event, process->stock_item.pool);

    if ((event & EV_TIMEOUT) == 0) {
        assert((event & EV_READ) != 0);

        char buffer;
        ssize_t nbytes = recv(fd, &buffer, sizeof(buffer), MSG_DONTWAIT);
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
 * clone() function
 *
 */

static int
delegate_stock_fn(void *ctx)
{
    struct delegate_info *info = (struct delegate_info *)ctx;

    install_default_signal_handlers();
    leave_signal_section(&info->signals);

    info->options->SetupStderr(true);

    namespace_options_setup(&info->options->ns);
    rlimit_options_apply(&info->options->rlimits);

    dup2(info->fds[1], STDIN_FILENO);
    close(info->fds[0]);
    close(info->fds[1]);

    clearenv();

    Exec e;
    e.Init();
    jail_wrapper_insert(e, &info->options->jail, NULL);
    e.Append(info->helper);
    e.DoExec();

    _exit(1);
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
    struct delegate_info *const info = (struct delegate_info *)_info;
    const struct child_options *const options = info->options;

    if (socketpair_cloexec(AF_UNIX, SOCK_STREAM, 0, info->fds) < 0) {
        GError *error = new_error_errno_msg("socketpair() failed: %s");
        stock_item_failed(item, error);
        return;
    }

    int clone_flags = SIGCHLD;
    clone_flags = namespace_options_clone_flags(&options->ns, clone_flags);

    /* avoid race condition due to libevent signal handler in child
       process */
    enter_signal_section(&info->signals);

    char stack[8192];
    long pid = clone(delegate_stock_fn, stack + sizeof(stack),
                     clone_flags, info);
    if (pid < 0) {
        GError *error = new_error_errno_msg("clone() failed");
        leave_signal_section(&info->signals);
        close(info->fds[0]);
        close(info->fds[1]);
        stock_item_failed(item, error);
        return;
    }

    leave_signal_section(&info->signals);

    close(info->fds[1]);

    process->uri = uri;
    process->pid = pid;
    process->fd = info->fds[0];

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
    return hstock_new(pool, &delegate_stock_class, NULL, 0, 16);
}

void
delegate_stock_get(struct hstock *delegate_stock, struct pool *pool,
                   const char *helper,
                   const struct child_options *options,
                   const struct stock_get_handler *handler, void *handler_ctx,
                   struct async_operation_ref *async_ref)
{
    assert(options != NULL);

    const char *uri = helper;

    char options_buffer[4096];
    *options->MakeId(options_buffer) = 0;
    if (*options_buffer != 0)
        uri = p_strcat(pool, helper, "|", options_buffer, NULL);

    auto info = NewFromPool<struct delegate_info>(pool);
    info->helper = helper;
    info->options = options;

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
