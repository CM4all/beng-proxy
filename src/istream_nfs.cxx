/*
 * istream implementation which reads a file from a NFS server.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "istream_nfs.hxx"
#include "istream-internal.h"
#include "istream_buffer.hxx"
#include "nfs_client.hxx"
#include "fifo_buffer.hxx"
#include "util/Cast.hxx"

#include <assert.h>
#include <string.h>

static const size_t NFS_BUFFER_SIZE = 32768;

struct istream_nfs {
    struct istream base;

    struct nfs_file_handle *handle;

    /**
     * The offset of the next "pread" call on the NFS server.
     */
    uint64_t offset;

    /**
     * The number of bytes that are remaining on the NFS server, not
     * including the amount of data that is already pending.
     */
    uint64_t remaining;

    /**
     * The number of bytes currently scheduled by nfs_pread_async().
     */
    size_t pending_read;

    /**
     * The number of bytes that shall be discarded from the
     * nfs_pread_async() result.  This is non-zero if istream_skip()
     * has been called while a read call was pending.
     */
    size_t discard_read;

    struct fifo_buffer *buffer;
};

extern const struct nfs_client_read_file_handler istream_nfs_read_handler;

/*
 * internal
 *
 */

static void
istream_nfs_schedule_read(struct istream_nfs *n)
{
    assert(n->pending_read == 0);

    const size_t max = n->buffer != nullptr
        ? fifo_buffer_space(n->buffer)
        : NFS_BUFFER_SIZE;
    size_t nbytes = n->remaining > max
        ? max
        : (size_t)n->remaining;

    const uint64_t offset = n->offset;

    n->offset += nbytes;
    n->remaining -= nbytes;
    n->pending_read = nbytes;

    nfs_client_read_file(n->handle, offset, nbytes,
                         &istream_nfs_read_handler, n);
}

/**
 * Check for end-of-file, and if there's more data to read, schedule
 * another read call.
 *
 * The input buffer must be empty.
 */
static void
istream_nfs_schedule_read_or_eof(struct istream_nfs *n)
{
    assert(n->buffer == nullptr || fifo_buffer_empty(n->buffer));

    if (n->pending_read > 0)
        return;

    if (n->remaining > 0) {
        /* read more */

        istream_nfs_schedule_read(n);
    } else {
        /* end of file */

        nfs_client_close_file(n->handle);
        istream_deinit_eof(&n->base);
    }
}

static void
istream_nfs_feed(struct istream_nfs *n, const void *data, size_t length)
{
    assert(length > 0);

    struct fifo_buffer *buffer = n->buffer;
    if (buffer == nullptr) {
        const uint64_t total_size = n->remaining + length;
        const size_t buffer_size = total_size > NFS_BUFFER_SIZE
            ? NFS_BUFFER_SIZE
            : (size_t)total_size;
        buffer = n->buffer = fifo_buffer_new(n->base.pool, buffer_size);
    }

    size_t max;
    void *p = fifo_buffer_write(buffer, &max);
    assert(max >= length);

    memcpy(p, data, length);
    fifo_buffer_append(buffer, length);
}

static void
istream_nfs_read_from_buffer(struct istream_nfs *n)
{
    assert(n->buffer != nullptr);

    size_t remaining = istream_buffer_consume(&n->base, n->buffer);
    if (remaining == 0 && n->pending_read == 0)
        istream_nfs_schedule_read_or_eof(n);
}

/*
 * nfs_client handler
 *
 */

static void
istream_nfs_read_data(const void *data, size_t _length, void *ctx)
{
    struct istream_nfs *n = (istream_nfs *)ctx;
    assert(n->pending_read > 0);
    assert(n->discard_read <= n->pending_read);
    assert(_length <= n->pending_read);

    if (_length < n->pending_read) {
        nfs_client_close_file(n->handle);
        GError *error = g_error_new_literal(g_file_error_quark(), 0,
                                            "premature end of file");
        istream_deinit_abort(&n->base, error);
        return;
    }

    const size_t discard = n->discard_read;
    const size_t length = n->pending_read - discard;
    n->pending_read = 0;
    n->discard_read = 0;

    if (length > 0)
        istream_nfs_feed(n, (const char *)data + discard, length);
    istream_nfs_read_from_buffer(n);
}

static void
istream_nfs_read_error(GError *error, void *ctx)
{
    struct istream_nfs *n = (istream_nfs *)ctx;
    assert(n->pending_read > 0);

    nfs_client_close_file(n->handle);
    istream_deinit_abort(&n->base, error);
}

const struct nfs_client_read_file_handler istream_nfs_read_handler = {
    istream_nfs_read_data,
    istream_nfs_read_error,
};

/*
 * istream implementation
 *
 */

static inline struct istream_nfs *
istream_to_nfs(struct istream *istream)
{
    return &ContainerCast2(*istream, &istream_nfs::base);
}

static off_t
istream_nfs_available(struct istream *istream, bool partial gcc_unused)
{
    struct istream_nfs *n = istream_to_nfs(istream);

    uint64_t available = n->remaining + n->pending_read - n->discard_read;
    if (n->buffer != nullptr)
        available += fifo_buffer_available(n->buffer);

    return available;
}

static off_t
istream_nfs_skip(struct istream *istream, off_t _length)
{
    struct istream_nfs *n = istream_to_nfs(istream);
    assert(n->discard_read <= n->pending_read);

    uint64_t length = _length;

    uint64_t result = 0;

    if (n->buffer != nullptr) {
        const uint64_t buffer_available = fifo_buffer_available(n->buffer);
        const uint64_t consume = length < buffer_available
            ? length
            : buffer_available;
        fifo_buffer_consume(n->buffer, consume);
        result += consume;
        length -= consume;
    }

    const uint64_t pending_available =
        n->pending_read - n->discard_read;
    uint64_t consume = length < pending_available
        ? length
        : pending_available;
    n->discard_read += consume;
    result += consume;
    length -= consume;

    if (length > n->remaining)
        length = n->remaining;

    n->remaining -= length;
    n->offset += length;
    result += length;

    return result;
}

static void
istream_nfs_read(struct istream *istream)
{
    struct istream_nfs *n = istream_to_nfs(istream);

    if (n->buffer != nullptr && !fifo_buffer_empty(n->buffer))
        istream_nfs_read_from_buffer(n);
    else
        istream_nfs_schedule_read_or_eof(n);
}

static void
istream_nfs_close(struct istream *istream)
{
    struct istream_nfs *const n = istream_to_nfs(istream);

    nfs_client_close_file(n->handle);
    istream_deinit(&n->base);
}

static const struct istream_class istream_nfs = {
    istream_nfs_available,
    istream_nfs_skip,
    istream_nfs_read,
    nullptr,
    istream_nfs_close,
};

/*
 * constructor
 *
 */

struct istream *
istream_nfs_new(struct pool *pool, struct nfs_file_handle *handle,
                uint64_t start, uint64_t end)
{
    assert(pool != nullptr);
    assert(handle != nullptr);
    assert(start <= end);

    struct istream_nfs *n = istream_new_macro(pool, nfs);
    n->handle = handle;
    n->offset = start;
    n->remaining = end - start;
    n->pending_read = 0;
    n->discard_read = 0;
    n->buffer = nullptr;

    return istream_struct_cast(&n->base);
}
