/*
 * Asynchronous local file access.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "istream_file.hxx"
#include "istream.hxx"
#include "buffered_io.hxx"
#include "system/fd_util.h"
#include "gerrno.h"
#include "pool.hxx"
#include "fb_pool.hxx"
#include "SliceFifoBuffer.hxx"
#include "event/TimerEvent.hxx"
#include "event/Callback.hxx"

#include <assert.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <limits.h>

/**
 * If EAGAIN occurs (on NFS), we try again after 100ms.  We can't
 * check EV_READ, because the kernel always indicates VFS files as
 * "readable without blocking".
 */
static const struct timeval file_retry_timeout = {
    .tv_sec = 0,
    .tv_usec = 100000,
};

struct FileIstream final : public Istream {
    int fd;

    FdType fd_type;

    /**
     * A timer to retry reading after EAGAIN.
     */
    TimerEvent event;

    off_t rest;
    SliceFifoBuffer buffer;
    const char *path;

    FileIstream(struct pool &p, EventLoop &event_loop,
                int _fd, FdType _fd_type, off_t _length,
                const char *_path)
        :Istream(p),
         fd(_fd), fd_type(_fd_type),
         event(event_loop, BIND_THIS_METHOD(EventCallback)),
         rest(_length),
         path(_path) {}

    ~FileIstream() {
        event.Cancel();
    }

    void CloseHandle() {
        if (fd < 0)
            return;

        event.Cancel();

        close(fd);
        fd = -1;

        buffer.FreeIfDefined(fb_pool_get());
    }

    void Abort(GError *error) {
        CloseHandle();
        DestroyError(error);
    }

    /**
     * @return the number of bytes still in the buffer
     */
    size_t SubmitBuffer() {
        return ConsumeFromBuffer(buffer);
    }

    void EofDetected() {
        assert(fd >= 0);

        CloseHandle();
        DestroyEof();
    }

    gcc_pure
    size_t GetMaxRead() const {
        if (rest != (off_t)-1 && rest < (off_t)INT_MAX)
            return (size_t)rest;
        else
            return INT_MAX;
    }

    void TryData();
    void TryDirect();

    void TryRead() {
        if (CheckDirect(fd_type))
            TryDirect();
        else
            TryData();
    }

    void EventCallback() {
        TryRead();
    }

    /* virtual methods from class Istream */

    off_t _GetAvailable(bool partial) override;
    off_t _Skip(gcc_unused off_t length) override;

    void _Read() override {
        event.Cancel();
        TryRead();
    }

    int _AsFd() override;
    void _Close() override {
        CloseHandle();
        Destroy();
    }
};

inline void
FileIstream::TryData()
{
    size_t buffer_rest = 0;

    if (buffer.IsNull()) {
        if (rest != 0)
            buffer.Allocate(fb_pool_get());
    } else {
        const size_t available = buffer.GetAvailable();
        if (available > 0) {
            buffer_rest = SubmitBuffer();
            if (buffer_rest == available)
                /* not a single byte was consumed: we may have been
                   closed, and we must bail out now */
                return;
        }
    }

    if (rest == 0) {
        if (buffer_rest == 0)
            EofDetected();
        return;
    }

    ssize_t nbytes = read_to_buffer(fd, buffer, GetMaxRead());
    if (nbytes == 0) {
        if (rest == (off_t)-1) {
            rest = 0;
            if (buffer_rest == 0)
                EofDetected();
        } else {
            GError *error =
                g_error_new(g_file_error_quark(), 0,
                            "premature end of file in '%s'", path);
            Abort(error);
        }
        return;
    } else if (nbytes == -1) {
        GError *error =
            g_error_new(errno_quark(), errno,
                        "failed to read from '%s': %s",
                        path, strerror(errno));
        Abort(error);
        return;
    } else if (nbytes > 0 && rest != (off_t)-1) {
        rest -= (off_t)nbytes;
        assert(rest >= 0);
    }

    assert(!buffer.IsEmpty());

    buffer_rest = SubmitBuffer();
    if (buffer_rest == 0 && rest == 0)
        EofDetected();
}

inline void
FileIstream::TryDirect()
{
    /* first consume the rest of the buffer */
    if (SubmitBuffer() > 0)
        return;

    if (rest == 0) {
        EofDetected();
        return;
    }

    ssize_t nbytes = InvokeDirect(fd_type, fd, GetMaxRead());
    if (nbytes == ISTREAM_RESULT_CLOSED)
        /* this stream was closed during the direct() callback */
        return;

    if (nbytes > 0 || nbytes == ISTREAM_RESULT_BLOCKING) {
        /* -2 means the callback wasn't able to consume any data right
           now */
        if (nbytes > 0 && rest != (off_t)-1) {
            rest -= (off_t)nbytes;
            assert(rest >= 0);
            if (rest == 0)
                EofDetected();
        }
    } else if (nbytes == ISTREAM_RESULT_EOF) {
        if (rest == (off_t)-1) {
            EofDetected();
        } else {
            GError *error =
                g_error_new(g_file_error_quark(), 0,
                            "premature end of file in '%s'", path);
            Abort(error);
        }
    } else if (errno == EAGAIN) {
        /* this should only happen for splice(SPLICE_F_NONBLOCK) from
           NFS files - unfortunately we cannot use EV_READ here, so we
           just install a timer which retries after 100ms */

        event.Add(file_retry_timeout);
    } else {
        /* XXX */
        GError *error =
            g_error_new(errno_quark(), errno,
                        "failed to read from '%s': %s",
                        path, strerror(errno));
        Abort(error);
    }
}

/*
 * istream implementation
 *
 */

off_t
FileIstream::_GetAvailable(bool partial)
{
    off_t available;
    if (rest != (off_t)-1)
        available = rest;
    else if (!partial)
        return (off_t)-1;
    else
        available = 0;

    available += buffer.GetAvailable();
    return available;
}

off_t
FileIstream::_Skip(off_t length)
{
    event.Cancel();

    if (rest == (off_t)-1)
        return (off_t)-1;

    if (length == 0)
        return 0;

    const size_t buffer_available = buffer.GetAvailable();
    if (length < off_t(buffer_available)) {
        buffer.Consume(length);
        Consumed(length);
        return length;
    }

    length -= buffer_available;
    buffer.Clear();

    if (length >= rest) {
        /* skip beyond EOF */

        length = rest;
        rest = 0;
    } else {
        /* seek the file descriptor */

        off_t ret = lseek(fd, length, SEEK_CUR);
        if (ret < 0)
            return -1;
        rest -= length;
    }

    off_t result = buffer_available + length;
    Consumed(result);
    return result;
}

int
FileIstream::_AsFd()
{
    int result_fd = fd;

    Destroy();

    return result_fd;
}

/*
 * constructor and public methods
 *
 */

Istream *
istream_file_fd_new(EventLoop &event_loop, struct pool &pool,
                    const char *path,
                    int fd, FdType fd_type, off_t length)
{
    assert(fd >= 0);
    assert(length >= -1);

    return NewIstream<FileIstream>(pool, event_loop, fd, fd_type, length, path);
}

Istream *
istream_file_stat_new(EventLoop &event_loop, struct pool &pool,
                      const char *path, struct stat &st,
                      GError **error_r)
{
    assert(path != nullptr);

    int fd = open_cloexec(path, O_RDONLY|O_NOCTTY, 0);
    if (fd < 0) {
        set_error_errno(error_r);
        g_prefix_error(error_r, "Failed to open %s: ", path);
        return nullptr;
    }

    if (fstat(fd, &st) < 0) {
        set_error_errno(error_r);
        g_prefix_error(error_r, "Failed to stat %s: ", path);
        close(fd);
        return nullptr;
    }

    FdType fd_type = FdType::FD_FILE;
    off_t size = st.st_size;

    if (S_ISCHR(st.st_mode)) {
        fd_type = FdType::FD_CHARDEV;
        size = -1;
    }

    return istream_file_fd_new(event_loop, pool, path, fd, fd_type, size);
}

Istream *
istream_file_new(EventLoop &event_loop, struct pool &pool,
                 const char *path, off_t length,
                 GError **error_r)
{
    assert(length >= -1);

    int fd = open_cloexec(path, O_RDONLY|O_NOCTTY, 0);
    if (fd < 0) {
        set_error_errno(error_r);
        g_prefix_error(error_r, "Failed to open %s: ", path);
        return nullptr;
    }

    return istream_file_fd_new(event_loop, pool,
                               path, fd, FdType::FD_FILE, length);
}

int
istream_file_fd(Istream &istream)
{
    auto &file = (FileIstream &)istream;
    assert(file.fd >= 0);
    return file.fd;
}

bool
istream_file_set_range(Istream &istream, off_t start, off_t end)
{
    assert(start >= 0);
    assert(end >= start);

    auto &file = (FileIstream &)istream;
    assert(file.fd >= 0);
    assert(file.rest >= 0);
    assert(file.buffer.IsNull());
    assert(end <= file.rest);

    if (start > 0 && lseek(file.fd, start, SEEK_CUR) < 0)
        return false;

    file.rest = end - start;
    return true;
}
