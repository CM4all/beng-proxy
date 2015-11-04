/*
 * Fork a process and connect its stdin and stdout to istreams.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "fork.hxx"
#include "system/fd_util.h"
#include "system/fd-util.h"
#include "istream/istream.hxx"
#include "istream/istream_pointer.hxx"
#include "istream/istream_oo.hxx"
#include "buffered_io.hxx"
#include "direct.hxx"
#include "event/Event.hxx"
#include "gerrno.h"
#include "pool.hxx"
#include "fb_pool.hxx"
#include "SliceFifoBuffer.hxx"
#include "event/Callback.hxx"
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

struct Fork final : Istream {
    int output_fd;
    Event output_event;

    SliceFifoBuffer buffer;

    IstreamPointer input;
    int input_fd;
    Event input_event;

    pid_t pid;

    child_callback_t callback;
    void *callback_ctx;

    Fork(struct pool &p, const char *name,
         Istream *_input, int _input_fd,
         int _output_fd,
         pid_t _pid, child_callback_t _callback, void *_ctx);

    bool CheckDirect() const {
        return Istream::CheckDirect(FdType::FD_PIPE);
    }

    void Cancel();

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

    void InputEventCallback() {
        input.Read();
    }

    void OutputEventCallback() {
        ReadFromOutput();
    }

    /* virtual methods from class Istream */

    void _Read() override;
    // TODO: implement int AsFd() override;
    void _Close() override;

    /* istream handler */

    size_t OnData(const void *data, size_t length);
#ifdef __linux
    ssize_t OnDirect(FdType type, int fd, size_t max_length);;
#endif
    void OnEof();
    void OnError(GError *error);
};

void
Fork::Cancel()
{
    assert(output_fd >= 0);

    if (input.IsDefined()) {
        assert(input_fd >= 0);

        input_event.Delete();
        close(input_fd);
        input.Close();
    }

    output_event.Delete();

    close(output_fd);
    output_fd = -1;

    if (pid >= 0)
        child_kill(pid);
}

inline bool
Fork::SendFromBuffer()
{
    assert(buffer.IsDefined());

    if (Istream::SendFromBuffer(buffer) == 0)
        return false;

    if (output_fd < 0) {
        if (buffer.IsEmpty()) {
            FreeBuffer();
            DestroyEof();
        }

        return false;
    }

    buffer.FreeIfEmpty(fb_pool_get());

    return true;
}

/*
 * input handler
 *
 */

inline size_t
Fork::OnData(const void *data, size_t length)
{
    assert(input_fd >= 0);

    ssize_t nbytes = write(input_fd, data, length);
    if (nbytes > 0)
        input_event.Add();
    else if (nbytes < 0) {
        if (errno == EAGAIN) {
            input_event.Add();
            return 0;
        }

        daemon_log(1, "write() to subprocess failed: %s\n",
                   strerror(errno));
        input_event.Delete();
        close(input_fd);
        input.ClearAndClose();
        return 0;
    }

    return (size_t)nbytes;
}

#ifdef __linux
inline ssize_t
Fork::OnDirect(FdType type, int fd, size_t max_length)
{
    assert(input_fd >= 0);

    ssize_t nbytes = istream_direct_to_pipe(type, fd, input_fd, max_length);
    if (nbytes > 0)
        input_event.Add();
    else if (nbytes < 0) {
        if (errno == EAGAIN) {
            if (!fd_ready_for_writing(input_fd)) {
                input_event.Add();
                return ISTREAM_RESULT_BLOCKING;
            }

            /* try again, just in case connection->fd has become ready
               between the first splice() call and
               fd_ready_for_writing() */
            nbytes = istream_direct_to_pipe(type, fd, input_fd, max_length);
        }
    }

    return nbytes;
}
#endif

inline void
Fork::OnEof()
{
    assert(input.IsDefined());
    assert(input_fd >= 0);

    input_event.Delete();
    close(input_fd);

    input.Clear();
}

void
Fork::OnError(GError *error)
{
    assert(input.IsDefined());
    assert(input_fd >= 0);

    FreeBuffer();

    input_event.Delete();
    close(input_fd);
    input.Clear();

    Cancel();
    DestroyError(error);
}

/*
 * event for fork.output_fd
 */

void
Fork::ReadFromOutput()
{
    assert(output_fd >= 0);

    if (!CheckDirect()) {
        buffer.AllocateIfNull(fb_pool_get());

        ssize_t nbytes = read_to_buffer(output_fd, buffer, INT_MAX);
        if (nbytes == -2) {
            /* XXX should not happen */
        } else if (nbytes > 0) {
            if (Istream::SendFromBuffer(buffer) > 0) {
                buffer.FreeIfEmpty(fb_pool_get());
                output_event.Add();
            }
        } else if (nbytes == 0) {
            Cancel();

            if (buffer.IsEmpty()) {
                FreeBuffer();
                DestroyEof();
            }
        } else if (errno == EAGAIN) {
            buffer.FreeIfEmpty(fb_pool_get());
            output_event.Add();

            if (input.IsDefined())
                /* the CGI may be waiting for more data from stdin */
                input.Read();
        } else {
            GError *error =
                new_error_errno_msg("failed to read from sub process");
            FreeBuffer();
            Cancel();
            DestroyError(error);
        }
    } else {
        if (Istream::ConsumeFromBuffer(buffer) > 0)
            /* there's data left in the buffer, which must be consumed
               before we can switch to "direct" transfer */
            return;

        buffer.FreeIfDefined(fb_pool_get());

        /* at this point, the handler might have changed inside
           Istream::ConsumeFromBuffer(), and the new handler might not
           support "direct" transfer - check again */
        if (!CheckDirect()) {
            output_event.Add();
            return;
        }

        ssize_t nbytes = InvokeDirect(FdType::FD_PIPE, output_fd, INT_MAX);
        if (nbytes == ISTREAM_RESULT_BLOCKING ||
            nbytes == ISTREAM_RESULT_CLOSED) {
            /* -2 means the callback wasn't able to consume any data right
               now */
        } else if (nbytes > 0) {
            output_event.Add();
        } else if (nbytes == ISTREAM_RESULT_EOF) {
            FreeBuffer();
            Cancel();
            DestroyEof();
        } else if (errno == EAGAIN) {
            output_event.Add();

            if (input.IsDefined())
                /* the CGI may be waiting for more data from stdin */
                input.Read();
        } else {
            GError *error =
                new_error_errno_msg("failed to read from sub process");
            FreeBuffer();
            Cancel();
            DestroyError(error);
        }
    }
}


/*
 * istream implementation
 *
 */

void
Fork::_Read()
{
    if (buffer.IsEmpty() || SendFromBuffer())
        ReadFromOutput();
}

void
Fork::_Close()
{
    FreeBuffer();

    if (output_fd >= 0)
        Cancel();

    Destroy();
}

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

inline
Fork::Fork(struct pool &p, const char *name,
           Istream *_input, int _input_fd,
           int _output_fd,
           pid_t _pid, child_callback_t _callback, void *_ctx)
    :Istream(p),
     output_fd(_output_fd),
     input(_input, MakeIstreamHandler<Fork>::handler, this, ISTREAM_TO_PIPE),
     input_fd(_input_fd),
     pid(_pid),
     callback(_callback), callback_ctx(_ctx)
{
    output_event.Set(output_fd, EV_READ,
                     MakeSimpleEventCallback(Fork, OutputEventCallback),
                     this);

    if (_input != nullptr) {
        input_event.Set(input_fd, EV_WRITE,
                        MakeSimpleEventCallback(Fork, InputEventCallback),
                        this);
        input_event.Add();
    }

    child_register(pid, name, fork_child_callback, this);
}

pid_t
beng_fork(struct pool *pool, const char *name,
          Istream *input, Istream **output_r,
          int clone_flags,
          int (*fn)(void *ctx), void *fn_ctx,
          child_callback_t callback, void *ctx,
          GError **error_r)
{
    assert(clone_flags & SIGCHLD);

    struct clone_ctx c;
    c.stdin_pipe[0] = -1;
    c.stdin_fd = -1;
    c.fn = fn;
    c.ctx = fn_ctx;

    if (input != nullptr) {
        c.stdin_fd = input->AsFd();
        if (c.stdin_fd >= 0)
            input = nullptr;
    }

    if (input != nullptr) {
        if (pipe_cloexec(c.stdin_pipe) < 0) {
            set_error_errno_msg(error_r, "pipe_cloexec() failed");
            input->CloseUnused();
            return -1;
        }

        if (fd_set_nonblock(c.stdin_pipe[1], 1) < 0) {
            set_error_errno_msg(error_r, "fcntl(O_NONBLOCK) failed");
            close(c.stdin_pipe[0]);
            close(c.stdin_pipe[1]);
            input->CloseUnused();
            return -1;
        }
    }

    if (pipe_cloexec(c.stdout_pipe) < 0) {
        set_error_errno_msg(error_r, "pipe() failed");

        if (input != nullptr) {
            close(c.stdin_pipe[0]);
            close(c.stdin_pipe[1]);
            input->CloseUnused();
        } else if (c.stdin_fd >= 0)
            close(c.stdin_fd);
        return -1;
    }

    if (fd_set_nonblock(c.stdout_pipe[0], 1) < 0) {
        set_error_errno_msg(error_r, "fcntl(O_NONBLOCK) failed");

        if (input != nullptr) {
            close(c.stdin_pipe[0]);
            close(c.stdin_pipe[1]);
            input->CloseUnused();
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
            input->CloseUnused();
        } else if (c.stdin_fd >= 0)
            close(c.stdin_fd);

        close(c.stdout_pipe[0]);
        close(c.stdout_pipe[1]);
    } else {
        auto f = NewFromPool<Fork>(*pool, *pool, name,
                                   input, c.stdin_pipe[1],
                                   c.stdout_pipe[0],
                                   pid, callback, ctx);

        if (input != nullptr) {
            close(c.stdin_pipe[0]);
        } else if (c.stdin_fd >= 0)
            close(c.stdin_fd);

        close(c.stdout_pipe[1]);

        /* XXX CLOEXEC */

        *output_r = f;
    }

    return pid;
}
