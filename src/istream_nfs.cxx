/*
 * istream implementation which reads a file from a NFS server.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "istream_nfs.hxx"
#include "istream/istream_internal.hxx"
#include "istream/istream_buffer.hxx"
#include "nfs_client.hxx"
#include "pool.hxx"
#include "util/Cast.hxx"
#include "util/ForeignFifoBuffer.hxx"

#include <assert.h>
#include <string.h>

static const size_t NFS_BUFFER_SIZE = 32768;

struct NfsIstream {
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
    size_t pending_read = 0;

    /**
     * The number of bytes that shall be discarded from the
     * nfs_pread_async() result.  This is non-zero if istream_skip()
     * has been called while a read call was pending.
     */
    size_t discard_read = 0;

    ForeignFifoBuffer<uint8_t> buffer;

    NfsIstream():buffer(nullptr) {}

    void ScheduleRead();

    /**
     * Check for end-of-file, and if there's more data to read, schedule
     * another read call.
     *
     * The input buffer must be empty.
     */
    void ScheduleReadOrEof();

    void Feed(const void *data, size_t length);

    void ReadFromBuffer();
};

void
NfsIstream::ScheduleReadOrEof()
{
    assert(buffer.IsEmpty());

    if (pending_read > 0)
        return;

    if (remaining > 0) {
        /* read more */

        ScheduleRead();
    } else {
        /* end of file */

        nfs_client_close_file(handle);
        istream_deinit_eof(&base);
    }
}

inline void
NfsIstream::Feed(const void *data, size_t length)
{
    assert(length > 0);

    if (buffer.IsNull()) {
        const uint64_t total_size = remaining + length;
        const size_t buffer_size = total_size > NFS_BUFFER_SIZE
            ? NFS_BUFFER_SIZE
            : (size_t)total_size;
        buffer.SetBuffer(PoolAlloc<uint8_t>(*base.pool, buffer_size),
                         buffer_size);
    }

    auto w = buffer.Write();
    assert(w.size >= length);

    memcpy(w.data, data, length);
    buffer.Append(length);
}

void
NfsIstream::ReadFromBuffer()
{
    assert(buffer.IsDefined());

    size_t remaining = istream_buffer_consume(&base, buffer);
    if (remaining == 0 && pending_read == 0)
        ScheduleReadOrEof();
}

/*
 * nfs_client handler
 *
 */

static void
istream_nfs_read_data(const void *data, size_t _length, void *ctx)
{
    auto *n = (NfsIstream *)ctx;
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
        n->Feed((const char *)data + discard, length);
    n->ReadFromBuffer();
}

static void
istream_nfs_read_error(GError *error, void *ctx)
{
    auto *n = (NfsIstream *)ctx;
    assert(n->pending_read > 0);

    nfs_client_close_file(n->handle);
    istream_deinit_abort(&n->base, error);
}

const struct nfs_client_read_file_handler istream_nfs_read_handler = {
    istream_nfs_read_data,
    istream_nfs_read_error,
};

inline void
NfsIstream::ScheduleRead()
{
    assert(pending_read == 0);

    const size_t max = buffer.IsDefined()
        ? buffer.Write().size
        : NFS_BUFFER_SIZE;
    size_t nbytes = remaining > max
        ? max
        : (size_t)remaining;

    const uint64_t read_offset = offset;

    offset += nbytes;
    remaining -= nbytes;
    pending_read = nbytes;

    nfs_client_read_file(handle, read_offset, nbytes,
                         &istream_nfs_read_handler, this);
}

/*
 * istream implementation
 *
 */

static inline NfsIstream *
istream_to_nfs(struct istream *istream)
{
    return &ContainerCast2(*istream, &NfsIstream::base);
}

static off_t
istream_nfs_available(struct istream *istream, bool partial gcc_unused)
{
    NfsIstream *n = istream_to_nfs(istream);

    return n->remaining + n->pending_read - n->discard_read +
        n->buffer.GetAvailable();
}

static off_t
istream_nfs_skip(struct istream *istream, off_t _length)
{
    NfsIstream *n = istream_to_nfs(istream);
    assert(n->discard_read <= n->pending_read);

    uint64_t length = _length;

    uint64_t result = 0;

    if (n->buffer.IsDefined()) {
        const uint64_t buffer_available = n->buffer.GetAvailable();
        const uint64_t consume = length < buffer_available
            ? length
            : buffer_available;
        n->buffer.Consume(consume);
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
    NfsIstream *n = istream_to_nfs(istream);

    if (!n->buffer.IsEmpty())
        n->ReadFromBuffer();
    else
        n->ScheduleReadOrEof();
}

static void
istream_nfs_close(struct istream *istream)
{
    NfsIstream *const n = istream_to_nfs(istream);

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

    auto *n = NewFromPool<NfsIstream>(*pool);
    istream_init(&n->base, &istream_nfs, pool);
    n->handle = handle;
    n->offset = start;
    n->remaining = end - start;

    return &n->base;
}
