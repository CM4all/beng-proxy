/*
 * Fork a process and connect its stdin and stdout to istreams.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "fork.hxx"
#include "fd_util.h"
#include "istream/istream_buffer.hxx"
#include "buffered_io.hxx"
#include "fd-util.h"
#include "direct.hxx"
#include "pevent.hxx"
#include "gerrno.h"
#include "fb_pool.hxx"
#include "SliceFifoBuffer.hxx"
#include "util/Cast.hxx"

#ifdef __linux
#include <fcntl.h>
#endif

#include <daemon/log.h>

#include <assert.h>
#include <errno.h>
#include <string.h>
#include <unistd.h>
#include <sched.h>
#include <signal.h>
#include <limits.h>

struct Fork {
    struct istream output;
    int output_fd;
    struct event output_event;

    SliceFifoBuffer buffer;

    struct istream *input;
    int input_fd;
    struct event input_event;

    pid_t pid;

    child_callback_t callback;
    void *callback_ctx;

    bool CheckDirect() const {
        return istream_check_direct(&output, FdType::FD_PIPE);
    }

    void Close();

    void FreeBuffer() {
        buffer.FreeIfDefined(fb_pool_get());
    }

    /**
     * Send data from the buffer.  Invokes the "eof" callback when the
     * buffer becomes empty and the pipe has been closed already.
     *
     * @return true if the caller shall read more data from the pipe
     */
    bool SendFromBuffer();

    void ReadFromOutput();
};

void
Fork::Close()
{
    assert(output_fd >= 0);

    if (input != nullptr) {
        assert(input_fd >= 0);

        p_event_del(&input_event, output.pool);
        close(input_fd);
        istream_close_handler(input);
    }

    p_event_del(&output_event, output.pool);

    close(output_fd);
    output_fd = -1;

    if (pid >= 0)
        child_kill(pid);
}

inline bool
Fork::SendFromBuffer()
{
    assert(buffer.IsDefined());

    if (istream_buffer_send(&output, buffer) == 0)
        return false;

    if (output_fd < 0) {
        if (buffer.IsEmpty()) {
            FreeBuffer();
            istream_deinit_eof(&output);
        }

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
    const auto f = (Fork *)ctx;

    assert(f->input_fd >= 0);

    ssize_t nbytes = write(f->input_fd, data, length);
    if (nbytes > 0)
        p_event_add(&f->input_event, nullptr,
                    f->output.pool, "fork_input_event");
    else if (nbytes < 0) {
        if (errno == EAGAIN) {
            p_event_add(&f->input_event, nullptr,
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
fork_input_direct(FdType type,
                  int fd, size_t max_length, void *ctx)
{
    const auto f = (Fork *)ctx;

    assert(f->input_fd >= 0);

    ssize_t nbytes = istream_direct_to_pipe(type, fd, f->input_fd, max_length);
    if (nbytes > 0)
        p_event_add(&f->input_event, nullptr,
                    f->output.pool, "fork_input_event");
    else if (nbytes < 0) {
        if (errno == EAGAIN) {
            if (!fd_ready_for_writing(f->input_fd)) {
                p_event_add(&f->input_event, nullptr,
                            f->output.pool, "fork_input_event");
                return ISTREAM_RESULT_BLOCKING;
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
    const auto f = (Fork *)ctx;

    assert(f->input != nullptr);
    assert(f->input_fd >= 0);

    p_event_del(&f->input_event, f->output.pool);
    close(f->input_fd);

    f->input = nullptr;
}

static void
fork_input_abort(GError *error, void *ctx)
{
    const auto f = (Fork *)ctx;

    assert(f->input != nullptr);
    assert(f->input_fd >= 0);

    f->FreeBuffer();

    p_event_del(&f->input_event, f->output.pool);
    close(f->input_fd);
    f->input = nullptr;

    f->Close();
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

/*
 * event for fork.output_fd
 */

void
Fork::ReadFromOutput()
{
    assert(output_fd >= 0);

    if (!CheckDirect()) {
        buffer.AllocateIfNull(fb_pool_get());

        ssize_t nbytes = read_to_buffer(output_fd,
                                        (ForeignFifoBuffer<uint8_t> &)buffer,
                                        INT_MAX);
        if (nbytes == -2) {
            /* XXX should not happen */
        } else if (nbytes > 0) {
            if (istream_buffer_send(&output, buffer) > 0)
                p_event_add(&output_event, nullptr,
                            output.pool, "fork_output_event");
        } else if (nbytes == 0) {
            Close();

            if (buffer.IsEmpty()) {
                FreeBuffer();
                istream_deinit_eof(&output);
            }
        } else if (errno == EAGAIN) {
            p_event_add(&output_event, nullptr,
                        output.pool, "fork_output_event");

            if (input != nullptr)
                /* the CGI may be waiting for more data from stdin */
                istream_read(input);
        } else {
            GError *error =
                new_error_errno_msg("failed to read from sub process");
            FreeBuffer();
            Close();
            istream_deinit_abort(&output, error);
        }
    } else {
        if (istream_buffer_consume(&output, buffer) > 0)
            /* there's data left in the buffer, which must be consumed
               before we can switch to "direct" transfer */
            return;

        /* at this point, the handler might have changed inside
           istream_buffer_consume(), and the new handler might not
           support "direct" transfer - check again */
        if (!CheckDirect()) {
            p_event_add(&output_event, nullptr,
                        output.pool, "fork_output_event");
            return;
        }

        ssize_t nbytes = istream_invoke_direct(&output, FdType::FD_PIPE,
                                               output_fd, INT_MAX);
        if (nbytes == ISTREAM_RESULT_BLOCKING ||
            nbytes == ISTREAM_RESULT_CLOSED) {
            /* -2 means the callback wasn't able to consume any data right
               now */
        } else if (nbytes > 0) {
            p_event_add(&output_event, nullptr,
                        output.pool, "fork_output_event");
        } else if (nbytes == ISTREAM_RESULT_EOF) {
            FreeBuffer();
            Close();
            istream_deinit_eof(&output);
        } else if (errno == EAGAIN) {
            p_event_add(&output_event, nullptr,
                        output.pool, "fork_output_event");

            if (input != nullptr)
                /* the CGI may be waiting for more data from stdin */
                istream_read(input);
        } else {
            GError *error =
                new_error_errno_msg("failed to read from sub process");
            FreeBuffer();
            Close();
            istream_deinit_abort(&output, error);
        }
    }
}

static void
fork_input_event_callback(int fd gcc_unused, short event gcc_unused,
                          void *ctx)
{
    const auto f = (Fork *)ctx;

    assert(f->input_fd == fd);
    assert(f->input != nullptr);

    p_event_consumed(&f->input_event, f->output.pool);

    istream_read(f->input);
}

static void
fork_output_event_callback(int fd gcc_unused, short event gcc_unused,
                           void *ctx)
{
    const auto f = (Fork *)ctx;

    assert(f->output_fd == fd);

    p_event_consumed(&f->output_event, f->output.pool);

    f->ReadFromOutput();
}


/*
 * istream implementation
 *
 */

static inline Fork *
istream_to_fork(struct istream *istream)
{
    return &ContainerCast2(*istream, &Fork::output);
}

static void
istream_fork_read(struct istream *istream)
{
    Fork *f = istream_to_fork(istream);

    if (f->buffer.IsEmpty() || f->SendFromBuffer())
        f->ReadFromOutput();
}

static void
istream_fork_close(struct istream *istream)
{
    Fork *f = istream_to_fork(istream);

    f->FreeBuffer();

    if (f->output_fd >= 0)
        f->Close();

    istream_deinit(&f->output);
}

static const struct istream_class istream_fork = {
    .read = istream_fork_read,
    .close = istream_fork_close,
};


/*
 * clone callback
 *
 */

struct clone_ctx {
    int stdin_pipe[2], stdin_fd, stdout_pipe[2];

    int (*fn)(void *ctx);
    void *ctx;
};

static int
beng_fork_fn(void *ctx)
{
    struct clone_ctx *c = (struct clone_ctx *)ctx;

    if (c->stdin_pipe[0] >= 0) {
        dup2(c->stdin_pipe[0], STDIN_FILENO);
        close(c->stdin_pipe[0]);
        close(c->stdin_pipe[1]);
    } else if (c->stdin_fd >= 0) {
        dup2(c->stdin_fd, STDIN_FILENO);
        close(c->stdin_fd);
    }

    dup2(c->stdout_pipe[1], STDOUT_FILENO);
    close(c->stdout_pipe[0]);
    close(c->stdout_pipe[1]);

    return c->fn(c->ctx);
}


/*
 * child callback
 *
 */

static void
fork_child_callback(int status, void *ctx)
{
    const auto f = (Fork *)ctx;

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
beng_fork(struct pool *pool, const char *name,
          struct istream *input, struct istream **output_r,
          int clone_flags,
          int (*fn)(void *ctx), void *fn_ctx,
          child_callback_t callback, void *ctx,
          GError **error_r)
{
    assert(clone_flags & SIGCHLD);

    struct clone_ctx c = {
        .stdin_pipe = { [0] = -1 },
        .stdin_fd = -1,
        .fn = fn,
        .ctx = fn_ctx,
    };

    if (input != nullptr) {
        c.stdin_fd = istream_as_fd(input);
        if (c.stdin_fd >= 0)
            input = nullptr;
    }

    if (input != nullptr) {
        if (pipe_cloexec(c.stdin_pipe) < 0) {
            set_error_errno_msg(error_r, "pipe_cloexec() failed");
            istream_close_unused(input);
            return -1;
        }

        if (fd_set_nonblock(c.stdin_pipe[1], 1) < 0) {
            set_error_errno_msg(error_r, "fcntl(O_NONBLOCK) failed");
            close(c.stdin_pipe[0]);
            close(c.stdin_pipe[1]);
            istream_close_unused(input);
            return -1;
        }
    }

    if (pipe_cloexec(c.stdout_pipe) < 0) {
        set_error_errno_msg(error_r, "pipe() failed");

        if (input != nullptr) {
            close(c.stdin_pipe[0]);
            close(c.stdin_pipe[1]);
            istream_close_unused(input);
        } else if (c.stdin_fd >= 0)
            close(c.stdin_fd);
        return -1;
    }

    if (fd_set_nonblock(c.stdout_pipe[0], 1) < 0) {
        set_error_errno_msg(error_r, "fcntl(O_NONBLOCK) failed");

        if (input != nullptr) {
            close(c.stdin_pipe[0]);
            close(c.stdin_pipe[1]);
            istream_close_unused(input);
        } else if (c.stdin_fd >= 0)
            close(c.stdin_fd);

        close(c.stdout_pipe[0]);
        close(c.stdout_pipe[1]);
        return -1;
    }

    char stack[8192];
    const pid_t pid = clone(beng_fork_fn, stack + sizeof(stack),
                            clone_flags, &c);
    if (pid < 0) {
        set_error_errno_msg(error_r, "fork() failed: %s");

        if (input != nullptr) {
            close(c.stdin_pipe[0]);
            close(c.stdin_pipe[1]);
            istream_close_unused(input);
        } else if (c.stdin_fd >= 0)
            close(c.stdin_fd);

        close(c.stdout_pipe[0]);
        close(c.stdout_pipe[1]);
    } else {
        Fork *f = (Fork *)
            istream_new(pool, &istream_fork, sizeof(*f));

        f->input = input;
        if (input != nullptr) {
            close(c.stdin_pipe[0]);
            f->input_fd = c.stdin_pipe[1];

            event_set(&f->input_event, f->input_fd, EV_WRITE,
                      fork_input_event_callback, f);
            p_event_add(&f->input_event, nullptr,
                        f->output.pool, "fork_input_event");

            istream_assign_handler(&f->input, input,
                                   &fork_input_handler, f,
                                   ISTREAM_TO_PIPE);
        } else if (c.stdin_fd >= 0)
            close(c.stdin_fd);

        close(c.stdout_pipe[1]);
        f->output_fd = c.stdout_pipe[0];
        event_set(&f->output_event, f->output_fd, EV_READ,
                  fork_output_event_callback, f);
        f->buffer.SetNull();

        f->pid = pid;
        f->callback = callback;
        f->callback_ctx = ctx;

        child_register(f->pid, name, fork_child_callback, f);

        /* XXX CLOEXEC */

        *output_r = &f->output;
    }

    return pid;
}
