/*
 * Asynchronous local file access.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "istream_file.hxx"
#include "istream_buffer.hxx"
#include "buffered_io.hxx"
#include "fd_util.h"
#include "gerrno.h"
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

struct file {
    struct istream stream;
    int fd;

    enum istream_direct fd_type;

    /**
     * A timer to retry reading after EAGAIN.
     */
    struct event event;

    off_t rest;
    SliceFifoBuffer buffer;
    const char *path;
};

static void
file_close(struct file *file)
{
    if (file->fd >= 0) {
        evtimer_del(&file->event);

        close(file->fd);
        file->fd = -1;
    }
}

static void
file_destroy(struct file *file)
{
    file_close(file);

    if (!file->buffer.IsNull())
        file->buffer.Free(fb_pool_get());
}

static void
file_abort(struct file *file, GError *error)
{
    file_destroy(file);

    istream_deinit_abort(&file->stream, error);
}

/**
 * @return the number of bytes still in the buffer
 */
static size_t
istream_file_invoke_data(struct file *file)
{
    return istream_buffer_consume(&file->stream, file->buffer);
}

static void
istream_file_eof_detected(struct file *file)
{
    assert(file->fd >= 0);

    file_destroy(file);

    istream_deinit_eof(&file->stream);
}

static inline size_t
istream_file_max_read(const struct file *file)
{
    if (file->rest != (off_t)-1 && file->rest < (off_t)INT_MAX)
        return (size_t)file->rest;
    else
        return INT_MAX;
}

static void
istream_file_try_data(struct file *file)
{
    size_t rest = 0;

    if (file->buffer.IsNull()) {
        if (file->rest != 0)
            file->buffer.Allocate(fb_pool_get());
    } else {
        const size_t available = file->buffer.GetAvailable();
        if (available > 0) {
            rest = istream_file_invoke_data(file);
            if (rest == available)
                /* not a single byte was consumed: we may have been
                   closed, and we must bail out now */
                return;
        }
    }

    if (file->rest == 0) {
        if (rest == 0)
            istream_file_eof_detected(file);
        return;
    }

    ForeignFifoBuffer<uint8_t> &buffer = file->buffer;
    ssize_t nbytes = read_to_buffer(file->fd, buffer,
                                    istream_file_max_read(file));
    if (nbytes == 0) {
        if (file->rest == (off_t)-1) {
            file->rest = 0;
            if (rest == 0)
                istream_file_eof_detected(file);
        } else {
            GError *error =
                g_error_new(g_file_error_quark(), 0,
                            "premature end of file in '%s'", file->path);
            file_abort(file, error);
        }
        return;
    } else if (nbytes == -1) {
        GError *error =
            g_error_new(errno_quark(), errno,
                        "failed to read from '%s': %s",
                        file->path, strerror(errno));
        file_abort(file, error);
        return;
    } else if (nbytes > 0 && file->rest != (off_t)-1) {
        file->rest -= (off_t)nbytes;
        assert(file->rest >= 0);
    }

    assert(!file->buffer.IsEmpty());

    rest = istream_file_invoke_data(file);
    if (rest == 0 && file->rest == 0)
        istream_file_eof_detected(file);
}

static void
istream_file_try_direct(struct file *file)
{
    assert(file->stream.handler->direct != nullptr);

    /* first consume the rest of the buffer */
    if (istream_file_invoke_data(file) > 0)
        return;

    if (file->rest == 0) {
        istream_file_eof_detected(file);
        return;
    }

    ssize_t nbytes = istream_invoke_direct(&file->stream,
                                           file->fd_type, file->fd,
                                           istream_file_max_read(file));
    if (nbytes == ISTREAM_RESULT_CLOSED)
        /* this stream was closed during the direct() callback */
        return;

    if (nbytes > 0 || nbytes == ISTREAM_RESULT_BLOCKING) {
        /* -2 means the callback wasn't able to consume any data right
           now */
        if (nbytes > 0 && file->rest != (off_t)-1) {
            file->rest -= (off_t)nbytes;
            assert(file->rest >= 0);
            if (file->rest == 0)
                istream_file_eof_detected(file);
        }
    } else if (nbytes == ISTREAM_RESULT_EOF) {
        if (file->rest == (off_t)-1) {
            istream_file_eof_detected(file);
        } else {
            GError *error =
                g_error_new(g_file_error_quark(), 0,
                            "premature end of file in '%s'", file->path);
            file_abort(file, error);
        }
    } else if (errno == EAGAIN) {
        /* this should only happen for splice(SPLICE_F_NONBLOCK) from
           NFS files - unfortunately we cannot use EV_READ here, so we
           just install a timer which retries after 100ms */

        evtimer_add(&file->event, &file_retry_timeout);
    } else {
        /* XXX */
        GError *error =
            g_error_new(errno_quark(), errno,
                        "failed to read from '%s': %s",
                        file->path, strerror(errno));
        file_abort(file, error);
    }
}

static void
file_try_read(struct file *file)
{
    if (istream_check_direct(&file->stream, file->fd_type))
        istream_file_try_direct(file);
    else
        istream_file_try_data(file);
}

static void
file_event_callback(gcc_unused int fd, gcc_unused short event,
                    void *ctx)
{
    struct file *file = (struct file *)ctx;

    file_try_read(file);
}


/*
 * istream implementation
 *
 */

static inline struct file *
istream_to_file(struct istream *istream)
{
    return &ContainerCast2(*istream, &file::stream);
}

static off_t
istream_file_available(struct istream *istream, bool partial)
{
    struct file *file = istream_to_file(istream);
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
    struct file *file = istream_to_file(istream);

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
    struct file *file = istream_to_file(istream);

    assert(file->stream.handler != nullptr);

    evtimer_del(&file->event);

    file_try_read(file);
}

static int
istream_file_as_fd(struct istream *istream)
{
    struct file *file = istream_to_file(istream);
    int fd = file->fd;

    evtimer_del(&file->event);
    istream_deinit(&file->stream);

    return fd;
}

static void
istream_file_close(struct istream *istream)
{
    struct file *file = istream_to_file(istream);

    file_destroy(file);

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
                    int fd, enum istream_direct fd_type, off_t length)
{
    struct file *file;

    assert(fd >= 0);
    assert(length >= -1);

    file = (struct file*)istream_new(pool, &istream_file, sizeof(*file));
    file->fd = fd;
    file->fd_type = fd_type;
    file->rest = length;
    file->buffer.SetNull();
    file->path = path;

    evtimer_set(&file->event, file_event_callback, file);

    return istream_struct_cast(&file->stream);
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

    enum istream_direct fd_type = ISTREAM_FILE;
    off_t size = st->st_size;

    if (S_ISCHR(st->st_mode)) {
        fd_type = ISTREAM_CHARDEV;
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

    return istream_file_fd_new(pool, path, fd, ISTREAM_FILE, length);
}

int
istream_file_fd(struct istream *istream)
{
    assert(istream != nullptr);
    assert(istream->cls == &istream_file);

    struct file *file = istream_to_file(istream);

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

    struct file *file = istream_to_file(istream);
    assert(file->fd >= 0);
    assert(file->rest >= 0);
    assert(file->buffer.IsNull());
    assert(end <= file->rest);

    if (start > 0 && lseek(file->fd, start, SEEK_CUR) < 0)
        return false;

    file->rest = end - start;
    return true;
}
