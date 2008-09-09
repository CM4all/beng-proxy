/*
 * Fork a process and connect its stdin and stdout to istreams.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "fork.h"
#include "socket-util.h"
#include "istream-buffer.h"
#include "buffered-io.h"
#include "event2.h"

#include <daemon/log.h>

#include <assert.h>
#include <errno.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <limits.h>

struct fork {
    struct istream output;
    int output_fd;
    struct event2 event;
    fifo_buffer_t buffer;

    istream_t input;
    int input_fd;

    pid_t pid;

    child_callback_t callback;
    void *callback_ctx;
};


static void
fork_close(struct fork *f)
{
    assert(f->output_fd >= 0);

    if (f->input != NULL) {
        assert(f->input_fd >= 0);

        close(f->input_fd);
        istream_close_handler(f->input);
    }

    event2_set(&f->event, 0);

    close(f->output_fd);
    f->output_fd = -1;
    f->buffer = NULL;

    if (f->pid >= 0) {
        kill(f->pid, SIGTERM);
        child_clear(f->pid);
        /* XXX SIGKILL? */
    }
}

/*
 * input handler
 *
 */

static size_t
fork_input_data(const void *data, size_t length, void *ctx)
{
    struct fork *f = ctx;
    ssize_t nbytes;

    assert(f->input_fd >= 0);

    nbytes = write(f->input_fd, data, length);
    if (nbytes < 0) {
        if (errno == EAGAIN)
            return 0;

        daemon_log(1, "write() to subprocess failed: %s\n",
                   strerror(errno));
        close(f->input_fd);
        f->input_fd = -1;
        istream_free_handler(&f->input);
        return 0;
    }

    return (size_t)nbytes;
}

static void
fork_input_eof(void *ctx)
{
    struct fork *f = ctx;

    assert(f->input_fd >= 0);

    close(f->input_fd);
    f->input_fd = -1;

    f->input = NULL;
}

static void
fork_input_abort(void *ctx)
{
    struct fork *f = ctx;

    assert(f->input_fd >= 0);

    f->input = NULL;

    fork_close(f);
    istream_deinit_abort(&f->output);
}

static const struct istream_handler fork_input_handler = {
    .data = fork_input_data,
    .eof = fork_input_eof,
    .abort = fork_input_abort,
};


/*
 * event for fork.output_fd
 */

static size_t
fork_flush_buffer(struct fork *f)
{
    if (f->buffer == NULL)
        return 0;

    return istream_buffer_consume(&f->output, f->buffer);
}

static void
fork_read_from_output(struct fork *f)
{
    ssize_t nbytes;

    assert(f->buffer == NULL || fifo_buffer_empty(f->buffer));

    if ((f->output.handler_direct & ISTREAM_PIPE) == 0) {
        if (f->buffer == NULL)
            f->buffer = fifo_buffer_new(f->output.pool, 1024);

        nbytes = read_to_buffer(f->output_fd, f->buffer, INT_MAX);
        if (nbytes == -2) {
            /* XXX should not happen */
        } else if (nbytes > 0) {
            size_t rest = fork_flush_buffer(f);
            if (rest == 0 && f->buffer != NULL)
                event2_set(&f->event, EV_READ);
        } else if (nbytes == 0) {
            fork_close(f);
            istream_deinit_eof(&f->output);
        } else if (errno == EAGAIN) {
            event2_set(&f->event, EV_READ);
        } else {
            daemon_log(1, "failed to read from sub process: %s\n",
                       strerror(errno));
            fork_close(f);
            istream_deinit_abort(&f->output);
        }
    } else {
        nbytes = istream_invoke_direct(&f->output, ISTREAM_PIPE,
                                       f->output_fd, INT_MAX);
        if (nbytes == -2) {
            /* -2 means the callback wasn't able to consume any data right
               now */
        } else if (nbytes > 0) {
            event2_set(&f->event, EV_READ);
        } else if (nbytes == 0) {
            fork_close(f);
            istream_deinit_eof(&f->output);
        } else if (errno == EAGAIN) {
            event2_set(&f->event, EV_READ);
        } else {
            daemon_log(1, "failed to read from sub process: %s\n",
                       strerror(errno));
            fork_close(f);
            istream_deinit_abort(&f->output);
        }
    }
}

static void
fork_output_event_callback(int fd __attr_unused, short event __attr_unused,
                           void *ctx)
{
    struct fork *f = ctx;

    assert(f->output_fd == fd);

    event2_reset(&f->event);
    fork_read_from_output(f);
}


/*
 * istream implementation
 *
 */

static inline struct fork *
istream_to_fork(istream_t istream)
{
    return (struct fork *)(((char*)istream) - offsetof(struct fork, output));
}

static void
istream_fork_read(istream_t istream)
{
    struct fork *f = istream_to_fork(istream);
    size_t rest;

    rest = fork_flush_buffer(f);
    if (rest == 0)
        fork_read_from_output(f);
}

static void
istream_fork_close(istream_t istream)
{
    struct fork *f = istream_to_fork(istream);

    fork_close(f);
    istream_deinit_abort(&f->output);
}

static const struct istream istream_fork = {
    .read = istream_fork_read,
    .close = istream_fork_close,
};


/*
 * child callback
 *
 */

static void
fork_child_callback(int status, void *ctx)
{
    struct fork *f = ctx;

    assert(f->pid >= 0);

    f->pid = -1;

    if (f->callback)
        f->callback(status, f->callback_ctx);
}


/*
 * constructor
 *
 */

pid_t
beng_fork(pool_t pool, istream_t input, istream_t *output_r,
          child_callback_t callback, void *ctx)
{
    int ret, stdin_pipe[2], stdout_pipe[2];
    pid_t pid;

    if (input != NULL) {
        ret = pipe(stdin_pipe);
        if (ret < 0)
            return -1;

        ret = socket_set_nonblock(stdin_pipe[1], 1);
        if (ret < 0) {
            daemon_log(1, "fcntl(O_NONBLOCK) failed: %s\n", strerror(errno));
            close(stdin_pipe[0]);
            close(stdin_pipe[1]);
            return -1;
        }
    }

    ret = pipe(stdout_pipe);
    if (ret < 0) {
        daemon_log(1, "pipe() failed: %s\n", strerror(errno));
        close(stdin_pipe[0]);
        close(stdin_pipe[1]);
        return -1;
    }

    ret = socket_set_nonblock(stdout_pipe[0], 1);
    if (ret < 0) {
        daemon_log(1, "fcntl(O_NONBLOCK) failed: %s\n", strerror(errno));
        close(stdin_pipe[0]);
        close(stdin_pipe[1]);
        close(stdout_pipe[0]);
        close(stdout_pipe[1]);
        return -1;
    }

    pid = fork();
    if (pid < 0) {
        daemon_log(1, "pipe() failed: %s\n", strerror(errno));
        close(stdin_pipe[0]);
        close(stdin_pipe[1]);
        close(stdout_pipe[0]);
        close(stdout_pipe[1]);
    } else if (pid == 0) {
        if (input != NULL) {
            dup2(stdin_pipe[0], STDIN_FILENO);
            close(stdin_pipe[0]);
            close(stdin_pipe[1]);
        }

        dup2(stdout_pipe[1], STDOUT_FILENO);
        close(stdout_pipe[0]);
    } else {
        struct fork *f = (struct fork *)
            istream_new(pool, &istream_fork, sizeof(*f));

        if (input != NULL) {
            close(stdin_pipe[0]);
            f->input_fd = stdin_pipe[1];

            istream_assign_handler(&f->input, input,
                                   &fork_input_handler, f,
                                   ISTREAM_FILE|ISTREAM_PIPE);
        } else
            f->input = NULL;

        close(stdout_pipe[1]);
        f->output_fd = stdout_pipe[0];
        event2_init(&f->event, f->output_fd,
                    fork_output_event_callback, f, NULL);
        f->buffer = NULL;

        f->pid = pid;
        f->callback = callback;
        f->callback_ctx = ctx;

        child_register(f->pid, fork_child_callback, f);

        /* XXX CLOEXEC */

        *output_r = istream_struct_cast(&f->output);
    }

    return pid;
}
