/*
 * Asynchronous local file access.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "istream-buffer.h"
#include "buffered-io.h"
#include "fd-util.h"

#include <daemon/log.h>

#include <assert.h>
#include <sys/types.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <limits.h>

struct file {
    struct istream stream;
    int fd;
    off_t rest;
    fifo_buffer_t buffer;
    const char *path;
};

static void
file_close(struct file *file)
{
    if (file->fd >= 0) {
        close(file->fd);
        file->fd = -1;
    }
}

static void
file_abort(struct file *file)
{
    file_close(file);

    istream_deinit_abort(&file->stream);
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

    file_close(file);

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
    size_t rest;
    ssize_t nbytes;

    if (file->buffer == NULL) {
        if (file->rest != 0)
            file->buffer = fifo_buffer_new(file->stream.pool, 4096);
        rest = 0;
    } else
        rest = istream_file_invoke_data(file);

    if (file->rest == 0) {
        if (rest == 0)
            istream_file_eof_detected(file);
        return;
    }

    nbytes = read_to_buffer(file->fd, file->buffer,
                            istream_file_max_read(file));
    if (nbytes == 0) {
        if (file->rest == (off_t)-1) {
            file->rest = 0;
            if (rest == 0)
                istream_file_eof_detected(file);
        } else {
            daemon_log(1, "premature end of file in '%s'\n",
                       file->path);
            file_abort(file);
        }
        return;
    } else if (nbytes == -1) {
        daemon_log(1, "failed to read from '%s': %s\n",
                   file->path, strerror(errno));
        file_abort(file);
        return;
    } else if (nbytes > 0 && file->rest != (off_t)-1) {
        file->rest -= (off_t)nbytes;
        assert(file->rest >= 0);
    }

    assert(!fifo_buffer_empty(file->buffer));

    rest = istream_file_invoke_data(file);
    if (rest == 0 && file->rest == 0)
        istream_file_eof_detected(file);
}

static void
istream_file_try_direct(struct file *file)
{
    ssize_t nbytes;

    assert(file->stream.handler->direct != NULL);

    /* first consume the rest of the buffer */
    if (file->buffer != NULL && istream_file_invoke_data(file) > 0)
        return;

    if (file->rest == 0) {
        istream_file_eof_detected(file);
        return;
    }

    nbytes = istream_invoke_direct(&file->stream, ISTREAM_FILE, file->fd,
                                   istream_file_max_read(file));
    if (nbytes > 0 || nbytes == -2) {
        /* -2 means the callback wasn't able to consume any data right
           now */
        if (nbytes > 0 && file->rest != (off_t)-1) {
            file->rest -= (off_t)nbytes;
            assert(file->rest >= 0);
            if (file->rest == 0)
                istream_file_eof_detected(file);
        }
    } else if (nbytes == 0) {
        if (file->rest == (off_t)-1) {
            istream_file_eof_detected(file);
        } else {
            daemon_log(1, "premature end of file in '%s'\n",
                       file->path);
            file_abort(file);
        }
    } else {
        /* XXX */
        daemon_log(1, "failed to read from '%s': %s\n",
                   file->path, strerror(errno));
        file_abort(file);
    }
}

static void
file_try_read(struct file *file)
{
    if ((file->stream.handler_direct & ISTREAM_FILE) == 0)
        istream_file_try_data(file);
    else
        istream_file_try_direct(file);
}


/*
 * istream implementation
 *
 */

static inline struct file *
istream_to_file(istream_t istream)
{
    return (struct file *)(((char*)istream) - offsetof(struct file, stream));
}

static off_t
istream_file_available(istream_t istream, bool partial)
{
    struct file *file = istream_to_file(istream);
    off_t available = 0;

    if (file->rest != (off_t)-1)
        available = file->rest;
    else if (!partial)
        return (off_t)-1;
    else
        available = 0;

    if (file->buffer != NULL) {
        const void *data;
        size_t length;

        data = fifo_buffer_read(file->buffer, &length);
        if (data != NULL)
            available += length;
    }

    return available;
}

static off_t
istream_file_skip(istream_t istream, off_t length)
{
    struct file *file = istream_to_file(istream);

    if (file->rest == (off_t)-1)
        return (off_t)-1;

    if (length == 0)
        return 0;

    if (file->buffer != NULL)
        /* clear the buffer; later we could optimize this function by
           flushing only the skipped number of bytes */
        fifo_buffer_clear(file->buffer);

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
istream_file_read(istream_t istream)
{
    struct file *file = istream_to_file(istream);

    assert(file->stream.handler != NULL);

    file_try_read(file);
}

static void
istream_file_close(istream_t istream)
{
    struct file *file = istream_to_file(istream);

    file_abort(file);
}

static const struct istream istream_file = {
    .available = istream_file_available,
    .skip = istream_file_skip,
    .read = istream_file_read,
    .close = istream_file_close,
};


/*
 * constructor and public methods
 *
 */

istream_t
istream_file_fd_new(pool_t pool, const char *path, int fd, off_t length)
{
    struct file *file;

    assert(fd >= 0);
    assert(length >= -1);

    fd_set_cloexec(fd);

    file = (struct file*)istream_new(pool, &istream_file, sizeof(*file));
    file->fd = fd;
    file->rest = length;
    file->buffer = NULL;
    file->path = path;

    return istream_struct_cast(&file->stream);
}

istream_t
istream_file_new(pool_t pool, const char *path, off_t length)
{
    int fd;

    assert(length >= -1);

    fd = open(path, O_RDONLY);
    if (fd < 0) {
        daemon_log(1, "failed to open '%s': %s\n",
                   path, strerror(errno));
        return NULL;
    }

    return istream_file_fd_new(pool, path, fd, length);
}

int
istream_file_fd(istream_t istream)
{
    struct file *file = istream_to_file(istream);

    assert(file->fd >= 0);

    return file->fd;
}

