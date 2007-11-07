/*
 * Asynchronous local file access.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "istream.h"
#include "fifo-buffer.h"
#include "buffered-io.h"

#include <daemon/log.h>

#include <assert.h>
#include <sys/types.h>
#include <sys/stat.h>
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

    istream_invoke_abort(&file->stream);
}

static inline struct file *
istream_to_file(istream_t istream)
{
    return (struct file *)(((char*)istream) - offsetof(struct file, stream));
}

/**
 * @return the number of bytes still in the buffer
 */
static size_t
istream_file_invoke_data(struct file *file)
{
    const void *data;
    size_t length, consumed;

    assert(file->buffer != NULL);
    
    data = fifo_buffer_read(file->buffer, &length);
    if (data == NULL)
        return 0;

    consumed = istream_invoke_data(&file->stream, data, length);
    assert(consumed <= length);

    fifo_buffer_consume(file->buffer, consumed);
    return length - consumed;
}

static void
istream_file_eof_detected(struct file *file)
{
    assert(file->fd >= 0);

    file_close(file);

    istream_invoke_eof(&file->stream);
}

static inline size_t
istream_file_max_read(const struct file *file)
{
    if (file->rest != (off_t)-1 && file->rest < INT_MAX)
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
        if (file->rest > 0)
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
istream_file_read(istream_t istream)
{
    struct file *file = istream_to_file(istream);

    assert(file->stream.handler != NULL);

    if ((file->stream.handler_direct & ISTREAM_FILE) == 0)
        istream_file_try_data(file);
    else
        istream_file_try_direct(file);
}

static void
istream_file_close(istream_t istream)
{
    struct file *file = istream_to_file(istream);

    file_abort(file);
}

static const struct istream istream_file = {
    .read = istream_file_read,
    .close = istream_file_close,
};

istream_t
istream_file_new(pool_t pool, const char *path, off_t length)
{
    struct file *file = p_malloc(pool, sizeof(*file));

    file->fd = open(path, O_RDONLY);
    if (file->fd < 0) {
        daemon_log(1, "failed to open '%s': %s\n",
                   path, strerror(errno));
        return NULL;
    }

    file->rest = length;
    file->buffer = NULL;
    file->path = path;
    file->stream = istream_file;
    file->stream.pool = pool;

    return istream_struct_cast(&file->stream);
}
