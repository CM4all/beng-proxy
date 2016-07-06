/*
 * An istream sink that copies data into a rubber allocation.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "sink_rubber.hxx"
#include "istream/Sink.hxx"
#include "async.hxx"
#include "rubber.hxx"
#include "pool.hxx"
#include "util/Cast.hxx"

#include <assert.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>

class RubberSink final : IstreamSink, Cancellable {
    Rubber &rubber;
    unsigned rubber_id;

    const size_t max_size;
    size_t position = 0;

    RubberSinkHandler &handler;

public:
    RubberSink(Rubber &_rubber, unsigned _rubber_id, size_t _max_size,
               RubberSinkHandler &_handler,
               Istream &_input,
               struct async_operation_ref &async_ref)
        :IstreamSink(_input, FD_ANY),
         rubber(_rubber), rubber_id(_rubber_id), max_size(_max_size),
         handler(_handler) {
        async_ref = *this;
    }

private:
    void FailTooLarge();
    void InvokeEof();

    /* virtual methods from class Cancellable */
    void Cancel() override;

    /* virtual methods from class IstreamHandler */
    size_t OnData(const void *data, size_t length) override;
    ssize_t OnDirect(FdType type, int fd, size_t max_length) override;
    void OnEof() override;
    void OnError(GError *error) override;
};

static ssize_t
fd_read(FdType type, int fd, void *p, size_t size)
{
    return IsAnySocket(type)
        ? recv(fd, p, size, MSG_DONTWAIT)
        : read(fd, p, size);
}

void
RubberSink::FailTooLarge()
{
    rubber_remove(&rubber, rubber_id);

    if (input.IsDefined())
        input.ClearAndClose();

    handler.RubberTooLarge();
}

void
RubberSink::InvokeEof()
{
    if (input.IsDefined())
        input.ClearAndClose();

    if (position == 0) {
        /* the stream was empty; remove the object from the rubber
           allocator */
        rubber_remove(&rubber, rubber_id);
        rubber_id = 0;
    } else
        rubber_shrink(&rubber, rubber_id, position);

    handler.RubberDone(rubber_id, position);
}

/*
 * istream handler
 *
 */

inline size_t
RubberSink::OnData(const void *data, size_t length)
{
    assert(position <= max_size);

    if (position + length > max_size) {
        /* too large, abort and invoke handler */

        FailTooLarge();
        return 0;
    }

    uint8_t *p = (uint8_t *)rubber_write(&rubber, rubber_id);
    memcpy(p + position, data, length);
    position += length;

    return length;
}

inline ssize_t
RubberSink::OnDirect(FdType type, int fd, size_t max_length)
{
    assert(position <= max_size);

    size_t length = max_size - position;
    if (length == 0) {
        /* already full, see what the file descriptor says */

        uint8_t dummy;
        ssize_t nbytes = fd_read(type, fd, &dummy, sizeof(dummy));
        if (nbytes > 0) {
            FailTooLarge();
            return ISTREAM_RESULT_CLOSED;
        }

        if (nbytes == 0) {
            InvokeEof();
            return ISTREAM_RESULT_CLOSED;
        }

        return ISTREAM_RESULT_ERRNO;
    }

    if (length > max_length)
        length = max_length;

    uint8_t *p = (uint8_t *)rubber_write(&rubber, rubber_id);
    p += position;

    ssize_t nbytes = fd_read(type, fd, p, length);
    if (nbytes > 0)
        position += (size_t)nbytes;

    return nbytes;
}

inline void
RubberSink::OnEof()
{
    assert(input.IsDefined());
    input.Clear();

    InvokeEof();
}

inline void
RubberSink::OnError(GError *error)
{
    assert(input.IsDefined());
    input.Clear();

    rubber_remove(&rubber, rubber_id);
    handler.RubberError(error);
}

/*
 * async operation
 *
 */

void
RubberSink::Cancel()
{
    rubber_remove(&rubber, rubber_id);

    if (input.IsDefined())
        input.ClearAndClose();
}

/*
 * constructor
 *
 */

void
sink_rubber_new(struct pool &pool, Istream &input,
                Rubber &rubber, size_t max_size,
                RubberSinkHandler &handler,
                struct async_operation_ref &async_ref)
{
    const off_t available = input.GetAvailable(true);
    if (available > (off_t)max_size) {
        input.CloseUnused();
        handler.RubberTooLarge();
        return;
    }

    const off_t size = input.GetAvailable(false);
    assert(size == -1 || size >= available);
    assert(size <= (off_t)max_size);
    if (size == 0) {
        input.CloseUnused();
        handler.RubberDone(0, 0);
        return;
    }

    const size_t allocate = size == -1
        ? max_size
        : (size_t)size;

    unsigned rubber_id = rubber_add(&rubber, allocate);
    if (rubber_id == 0) {
        input.CloseUnused();
        handler.RubberOutOfMemory();
        return;
    }

    NewFromPool<RubberSink>(pool, rubber, rubber_id, allocate,
                            handler,
                            input, async_ref);
}
