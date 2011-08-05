/*
 * Fork a process and connect its stdin and stdout to istreams.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "fork.h"
#include "fd_util.h"
#include "istream-buffer.h"
#include "buffered-io.h"
#include "fd-util.h"
#include "direct.h"
#include "pevent.h"

#ifdef __linux
#include <fcntl.h>
#endif

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
    struct event output_event;
    struct fifo_buffer *buffer;

    istream_t input;
    int input_fd;
    struct event input_event;

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

        p_event_del(&f->input_event, f->output.pool);
        close(f->input_fd);
        istream_close_handler(f->input);
    }

    p_event_del(&f->output_event, f->output.pool);

    close(f->output_fd);
    f->output_fd = -1;

    if (f->pid >= 0) {
        kill(f->pid, SIGTERM);
        child_clear(f->pid);
        /* XXX SIGKILL? */
    }
}

/**
 * Send data from the buffer.  Invokes the "eof" callback when the
 * buffer becomes empty and the pipe has been closed already.
 *
 * @return true if the caller shall read more data from the pipe
 */
static bool
fork_buffer_send(struct fork *f)
{
    assert(f->buffer != NULL);

    if (istream_buffer_send(&f->output, f->buffer) == 0)
        return false;

    if (f->output_fd < 0) {
        if (fifo_buffer_empty(f->buffer))
            istream_deinit_eof(&f->output);
        return false;
    }

    return true;
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
    if (nbytes > 0)
        p_event_add(&f->input_event, NULL,
                    f->output.pool, "fork_input_event");
    else if (nbytes < 0) {
        if (errno == EAGAIN) {
            p_event_add(&f->input_event, NULL,
                        f->output.pool, "fork_input_event");
            return 0;
        }

        daemon_log(1, "write() to subprocess failed: %s\n",
                   strerror(errno));
        p_event_del(&f->input_event, f->output.pool);
        close(f->input_fd);
        istream_free_handler(&f->input);
        return 0;
    }

    return (size_t)nbytes;
}

#ifdef __linux
static ssize_t
fork_input_direct(istream_direct_t type,
                  int fd, size_t max_length, void *ctx)
{
    struct fork *f = ctx;
    ssize_t nbytes;

    assert(f->input_fd >= 0);

    nbytes = istream_direct_to_pipe(type, fd, f->input_fd, max_length);
    if (nbytes > 0)
        p_event_add(&f->input_event, NULL,
                    f->output.pool, "fork_input_event");
    else if (nbytes < 0) {
        if (errno == EAGAIN) {
            if (!fd_ready_for_writing(f->input_fd)) {
                p_event_add(&f->input_event, NULL,
                            f->output.pool, "fork_input_event");
                return -2;
            }

            /* try again, just in case connection->fd has become ready
               between the first splice() call and
               fd_ready_for_writing() */
            nbytes = istream_direct_to_pipe(type, fd, f->input_fd, max_length);
        }
    }

    return nbytes;
}
#endif

static void
fork_input_eof(void *ctx)
{
    struct fork *f = ctx;

    assert(f->input != NULL);
    assert(f->input_fd >= 0);

    p_event_del(&f->input_event, f->output.pool);
    close(f->input_fd);

    f->input = NULL;
}

static void
fork_input_abort(GError *error, void *ctx)
{
    struct fork *f = ctx;

    assert(f->input != NULL);
    assert(f->input_fd >= 0);

    p_event_del(&f->input_event, f->output.pool);
    close(f->input_fd);
    f->input = NULL;

    fork_close(f);
    istream_deinit_abort(&f->output, error);
}

static const struct istream_handler fork_input_handler = {
    .data = fork_input_data,
#ifdef __linux
    .direct = fork_input_direct,
#endif
    .eof = fork_input_eof,
    .abort = fork_input_abort,
};

static bool
fork_check_direct(const struct fork *f)
{
    return istream_check_direct(&f->output, ISTREAM_PIPE);
}

/*
 * event for fork.output_fd
 */

static void
fork_read_from_output(struct fork *f)
{
    assert(f->output_fd >= 0);

    if (!fork_check_direct(f)) {
        if (f->buffer == NULL)
            f->buffer = fifo_buffer_new(f->output.pool, 1024);

        ssize_t nbytes = read_to_buffer(f->output_fd, f->buffer, INT_MAX);
        if (nbytes == -2) {
            /* XXX should not happen */
        } else if (nbytes > 0) {
            if (istream_buffer_send(&f->output, f->buffer) > 0)
                p_event_add(&f->output_event, NULL,
                            f->output.pool, "fork_output_event");
        } else if (nbytes == 0) {
            fork_close(f);

            if (fifo_buffer_empty(f->buffer))
                istream_deinit_eof(&f->output);
        } else if (errno == EAGAIN) {
            p_event_add(&f->output_event, NULL,
                        f->output.pool, "fork_output_event");

            if (f->input != NULL)
                /* the CGI may be waiting for more data from stdin */
                istream_read(f->input);
        } else {
            GError *error = g_error_new(g_file_error_quark(), errno,
                                        "failed to read from sub process: %s",
                                        strerror(errno));
            fork_close(f);
            istream_deinit_abort(&f->output, error);
        }
    } else {
        if (f->buffer != NULL &&
            istream_buffer_consume(&f->output, f->buffer) > 0)
            /* there's data left in the buffer, which must be consumed
               before we can switch to "direct" transfer */
            return;

        /* at this point, the handler might have changed inside
           istream_buffer_consume(), and the new handler might not
           support "direct" transfer - check again */
        if (!fork_check_direct(f)) {
            p_event_add(&f->output_event, NULL,
                        f->output.pool, "fork_output_event");
            return;
        }

        ssize_t nbytes = istream_invoke_direct(&f->output, ISTREAM_PIPE,
                                               f->output_fd, INT_MAX);
        if (nbytes == -2 || nbytes == -3) {
            /* -2 means the callback wasn't able to consume any data right
               now */
        } else if (nbytes > 0) {
            p_event_add(&f->output_event, NULL,
                        f->output.pool, "fork_output_event");
        } else if (nbytes == 0) {
            fork_close(f);
            istream_deinit_eof(&f->output);
        } else if (errno == EAGAIN) {
            p_event_add(&f->output_event, NULL,
                        f->output.pool, "fork_output_event");

            if (f->input != NULL)
                /* the CGI may be waiting for more data from stdin */
                istream_read(f->input);
        } else {
            GError *error = g_error_new(g_file_error_quark(), errno,
                                        "failed to read from sub process: %s",
                                        strerror(errno));
            fork_close(f);
            istream_deinit_abort(&f->output, error);
        }
    }
}

static void
fork_input_event_callback(int fd __attr_unused, short event __attr_unused,
                          void *ctx)
{
    struct fork *f = ctx;

    assert(f->input_fd == fd);
    assert(f->input != NULL);

    p_event_consumed(&f->input_event, f->output.pool);

    istream_read(f->input);
}

static void
fork_output_event_callback(int fd __attr_unused, short event __attr_unused,
                           void *ctx)
{
    struct fork *f = ctx;

    assert(f->output_fd == fd);

    p_event_consumed(&f->output_event, f->output.pool);

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

    if (f->buffer == NULL || fifo_buffer_empty(f->buffer) ||
        fork_buffer_send(f))
        fork_read_from_output(f);
}

static void
istream_fork_close(istream_t istream)
{
    struct fork *f = istream_to_fork(istream);

    if (f->output_fd >= 0)
        fork_close(f);

    istream_deinit(&f->output);
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
          child_callback_t callback, void *ctx,
          GError **error_r)
{
    int ret, stdin_pipe[2], stdin_fd, stdout_pipe[2];
    pid_t pid;

    if (input != NULL) {
        stdin_fd = istream_as_fd(input);
        if (stdin_fd >= 0)
            input = NULL;
    } else
        stdin_fd = -1;

    if (input != NULL) {
        ret = pipe_cloexec(stdin_pipe);
        if (ret < 0) {
            g_set_error(error_r, fork_quark(), errno,
                        "pipe_cloexec() failed: %s", strerror(errno));
            return -1;
        }

        ret = fd_set_nonblock(stdin_pipe[1], 1);
        if (ret < 0) {
            g_set_error(error_r, fork_quark(), errno,
                        "fcntl(O_NONBLOCK) failed: %s", strerror(errno));
            close(stdin_pipe[0]);
            close(stdin_pipe[1]);
            return -1;
        }
    }

    ret = pipe_cloexec(stdout_pipe);
    if (ret < 0) {
        g_set_error(error_r, fork_quark(), errno,
                    "pipe() failed: %s", strerror(errno));

        if (input != NULL) {
            close(stdin_pipe[0]);
            close(stdin_pipe[1]);
        } else if (stdin_fd >= 0)
            close(stdin_fd);

        return -1;
    }

    ret = fd_set_nonblock(stdout_pipe[0], 1);
    if (ret < 0) {
        g_set_error(error_r, fork_quark(), errno,
                    "fcntl(O_NONBLOCK) failed: %s", strerror(errno));

        if (input != NULL) {
            close(stdin_pipe[0]);
            close(stdin_pipe[1]);
        } else if (stdin_fd >= 0)
            close(stdin_fd);

        close(stdout_pipe[0]);
        close(stdout_pipe[1]);
        return -1;
    }

    pid = fork();
    if (pid < 0) {
        g_set_error(error_r, fork_quark(), errno,
                    "fork() failed: %s", strerror(errno));

        if (input != NULL) {
            close(stdin_pipe[0]);
            close(stdin_pipe[1]);
        } else if (stdin_fd >= 0)
            close(stdin_fd);

        close(stdout_pipe[0]);
        close(stdout_pipe[1]);
    } else if (pid == 0) {
        if (input != NULL) {
            dup2(stdin_pipe[0], STDIN_FILENO);
            close(stdin_pipe[0]);
            close(stdin_pipe[1]);
        } else if (stdin_fd >= 0) {
            dup2(stdin_fd, STDIN_FILENO);
            close(stdin_fd);
        }

        dup2(stdout_pipe[1], STDOUT_FILENO);
        close(stdout_pipe[0]);
        close(stdout_pipe[1]);
    } else {
        struct fork *f = (struct fork *)
            istream_new(pool, &istream_fork, sizeof(*f));

        f->input = input;
        if (input != NULL) {
            close(stdin_pipe[0]);
            f->input_fd = stdin_pipe[1];

            event_set(&f->input_event, f->input_fd, EV_WRITE,
                      fork_input_event_callback, f);
            p_event_add(&f->input_event, NULL,
                        f->output.pool, "fork_input_event");

            istream_assign_handler(&f->input, input,
                                   &fork_input_handler, f,
                                   ISTREAM_TO_PIPE);
        } else if (stdin_fd >= 0)
            close(stdin_fd);

        close(stdout_pipe[1]);
        f->output_fd = stdout_pipe[0];
        event_set(&f->output_event, f->output_fd, EV_READ,
                  fork_output_event_callback, f);
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
