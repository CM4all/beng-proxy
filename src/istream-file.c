/*
 * Asynchronous local file access.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "istream.h"
#include "fifo-buffer.h"
#include "buffered-io.h"

#include <assert.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <limits.h>

struct file {
    struct istream stream;
    int fd;
    fifo_buffer_t buffer;
    const char *path;
};

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

    close(file->fd);
    file->fd = -1;

    istream_invoke_eof(&file->stream);
    istream_close(&file->stream);
}

static void
istream_file_read(istream_t istream)
{
    struct file *file = istream_to_file(istream);
    size_t rest;
    ssize_t nbytes;

    rest = istream_file_invoke_data(file);

    if (file->fd < 0) {
        if (rest == 0)
            istream_file_eof_detected(file);
        return;
    }

    nbytes = read_to_buffer(file->fd, file->buffer);
    if (nbytes == 0) {
        if (rest == 0)
            istream_file_eof_detected(file);
        return;
    } else if (nbytes == -1) {
        fprintf(stderr, "failed to read from '%s': %s\n",
                file->path, strerror(errno));
        istream_close(istream);
        return;
    }

    assert(!fifo_buffer_empty(file->buffer));

    istream_file_invoke_data(file);
}

static void
istream_file_direct(istream_t istream)
{
    struct file *file = istream_to_file(istream);
    ssize_t nbytes;

    /* first consume the rest of the buffer */
    if (istream_file_invoke_data(file) > 0)
        return;

    if (file->fd < 0) {
        istream_file_eof_detected(file);
        return;
    }

    nbytes = istream_invoke_direct(istream, file->fd, INT_MAX);
    if (nbytes > 0 || nbytes == -2) {
        /* -2 means the callback wasn't able to consume any data right
            now */
    } else if (nbytes == 0) {
        pool_ref(istream->pool);
        istream_invoke_eof(istream);
        istream_close(istream);
        pool_unref(istream->pool);
    } else {
        /* XXX */
        fprintf(stderr, "failed to read from '%s': %s\n",
                file->path, strerror(errno));
        istream_close(istream);
    }
}

static void
istream_file_close(istream_t istream)
{
    struct file *file = istream_to_file(istream);

    if (file->fd >= 0) {
        close(file->fd);
        file->fd = -1;
    }

    istream_invoke_free(istream);
}

static const struct istream istream_file = {
    .read = istream_file_read,
    .direct = istream_file_direct,
    .close = istream_file_close,
};

istream_t
istream_file_new(pool_t pool, const char *path)
{
    struct file *file = p_malloc(pool, sizeof(*file));

    file->fd = open(path, O_RDONLY);
    if (file->fd < 0) {
        fprintf(stderr, "failed to open '%s': %s\n",
                path, strerror(errno));
        return NULL;
    }

    file->buffer = fifo_buffer_new(pool, 4096);
    file->path = path;
    file->stream = istream_file;
    file->stream.pool = pool;

    return &file->stream;
}
