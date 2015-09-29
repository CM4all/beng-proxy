/*
 * Asynchronous local file access.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "istream_file.hxx"
#include "istream_buffer.hxx"
#include "buffered_io.hxx"
#include "system/fd_util.h"
#include "gerrno.h"
#include "pool.hxx"
#include "fb_pool.hxx"
#include "SliceFifoBuffer.hxx"
#include "util/Cast.hxx"

#include <assert.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <limits.h>

#include <event.h>

/**
 * If EAGAIN occurs (on NFS), we try again after 100ms.  We can't
 * check EV_READ, because the kernel always indicates VFS files as
 * "readable without blocking".
 */
static const struct timeval file_retry_timeout = {
    .tv_sec = 0,
    .tv_usec = 100000,
};

struct FileIstream {
    struct istream stream;
    int fd;

    FdType fd_type;

    /**
     * A timer to retry reading after EAGAIN.
     */
    struct event event;

    off_t rest;
    SliceFifoBuffer buffer;
    const char *path;

    void CloseHandle() {
        if (fd < 0)
            return;

        evtimer_del(&event);

        close(fd);
        fd = -1;

        buffer.FreeIfDefined(fb_pool_get());
    }

    void Abort(GError *error) {
        CloseHandle();

        istream_deinit_abort(&stream, error);
    }

    /**
     * @return the number of bytes still in the buffer
     */
    size_t SubmitBuffer() {
        return istream_buffer_consume(&stream, buffer);
    }

    void EofDetected() {
        assert(fd >= 0);

        CloseHandle();
        istream_deinit_eof(&stream);
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
        if (istream_check_direct(&stream, fd_type))
            TryDirect();
        else
            TryData();
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
    assert(stream.handler->direct != nullptr);

    /* first consume the rest of the buffer */
    if (SubmitBuffer() > 0)
        return;

    if (rest == 0) {
        EofDetected();
        return;
    }

    ssize_t nbytes = istream_invoke_direct(&stream,
                                           fd_type, fd,
                                           GetMaxRead());
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

        evtimer_add(&event, &file_retry_timeout);
    } else {
        /* XXX */
        GError *error =
            g_error_new(errno_quark(), errno,
                        "failed to read from '%s': %s",
                        path, strerror(errno));
        Abort(error);
    }
}

static void
file_event_callback(gcc_unused int fd, gcc_unused short event,
                    void *ctx)
{
    FileIstream *file = (FileIstream *)ctx;

    file->TryRead();
}


/*
 * istream implementation
 *
 */

static inline FileIstream *
istream_to_file(struct istream *istream)
{
    return &ContainerCast2(*istream, &FileIstream::stream);
}

static off_t
istream_file_available(struct istream *istream, bool partial)
{
    FileIstream *file = istream_to_file(istream);
    off_t available = 0;

    if (file->rest != (off_t)-1)
        available = file->rest;
    else if (!partial)
        return (off_t)-1;
    else
        available = 0;

    available += file->buffer.GetAvailable();
    return available;
}

static off_t
istream_file_skip(struct istream *istream, off_t length)
{
    FileIstream *file = istream_to_file(istream);

    evtimer_del(&file->event);

    if (file->rest == (off_t)-1)
        return (off_t)-1;

    if (length == 0)
        return 0;

    /* clear the buffer; later we could optimize this function by
       flushing only the skipped number of bytes */
    file->buffer.Clear();

    if (length >= file->rest) {
        /* skip beyond EOF */

        length = file->rest;
        file->rest = 0;
    } else {
        /* seek the file descriptor */

        off_t ret = lseek(file->fd, length, SEEK_CUR);
        if (ret < 0)
            return -1;
        file->rest -= length;
    }

    return length;
}

static void
istream_file_read(struct istream *istream)
{
    FileIstream *file = istream_to_file(istream);

    assert(file->stream.handler != nullptr);

    evtimer_del(&file->event);

    file->TryRead();
}

static int
istream_file_as_fd(struct istream *istream)
{
    FileIstream *file = istream_to_file(istream);
    int fd = file->fd;

    evtimer_del(&file->event);
    istream_deinit(&file->stream);

    return fd;
}

static void
istream_file_close(struct istream *istream)
{
    FileIstream *file = istream_to_file(istream);

    file->CloseHandle();

    istream_deinit(&file->stream);
}

static const struct istream_class istream_file = {
    .available = istream_file_available,
    .skip = istream_file_skip,
    .read = istream_file_read,
    .as_fd = istream_file_as_fd,
    .close = istream_file_close,
};


/*
 * constructor and public methods
 *
 */

struct istream *
istream_file_fd_new(struct pool *pool, const char *path,
                    int fd, FdType fd_type, off_t length)
{
    assert(fd >= 0);
    assert(length >= -1);

    auto file = NewFromPool<FileIstream>(*pool);
    istream_init(&file->stream, &istream_file, pool);
    file->fd = fd;
    file->fd_type = fd_type;
    file->rest = length;
    file->buffer.SetNull();
    file->path = path;

    evtimer_set(&file->event, file_event_callback, file);

    return &file->stream;
}

struct istream *
istream_file_stat_new(struct pool *pool, const char *path, struct stat *st,
                      GError **error_r)
{
    assert(path != nullptr);
    assert(st != nullptr);

    int fd = open_cloexec(path, O_RDONLY|O_NOCTTY, 0);
    if (fd < 0) {
        set_error_errno(error_r);
        g_prefix_error(error_r, "Failed to open %s: ", path);
        return nullptr;
    }

    if (fstat(fd, st) < 0) {
        set_error_errno(error_r);
        g_prefix_error(error_r, "Failed to stat %s: ", path);
        close(fd);
        return nullptr;
    }

    FdType fd_type = FdType::FD_FILE;
    off_t size = st->st_size;

    if (S_ISCHR(st->st_mode)) {
        fd_type = FdType::FD_CHARDEV;
        size = -1;
    }

    return istream_file_fd_new(pool, path, fd, fd_type, size);
}

struct istream *
istream_file_new(struct pool *pool, const char *path, off_t length,
                 GError **error_r)
{
    assert(length >= -1);

    int fd = open_cloexec(path, O_RDONLY|O_NOCTTY, 0);
    if (fd < 0) {
        set_error_errno(error_r);
        g_prefix_error(error_r, "Failed to open %s: ", path);
        return nullptr;
    }

    return istream_file_fd_new(pool, path, fd, FdType::FD_FILE, length);
}

int
istream_file_fd(struct istream *istream)
{
    assert(istream != nullptr);
    assert(istream->cls == &istream_file);

    FileIstream *file = istream_to_file(istream);

    assert(file->fd >= 0);

    return file->fd;
}

bool
istream_file_set_range(struct istream *istream, off_t start, off_t end)
{
    assert(istream != nullptr);
    assert(istream->cls == &istream_file);
    assert(start >= 0);
    assert(end >= start);

    FileIstream *file = istream_to_file(istream);
    assert(file->fd >= 0);
    assert(file->rest >= 0);
    assert(file->buffer.IsNull());
    assert(end <= file->rest);

    if (start > 0 && lseek(file->fd, start, SEEK_CUR) < 0)
        return false;

    file->rest = end - start;
    return true;
}
