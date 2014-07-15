/*
 * Wrapper for a socket descriptor with (optional) filter for input
 * and output.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_FILTERED_SOCKET_HXX
#define BENG_PROXY_FILTERED_SOCKET_HXX

#include "buffered_socket.hxx"

#include <pthread.h>

struct fifo_buffer;
struct FilteredSocket;

struct SocketFilter {
    void (*init)(FilteredSocket *s, void *ctx);

    /**
     * Data has been read from the socket into the input buffer.  Call
     * filtered_socket_internal_consumed() each time you consume data
     * from the given buffer.
     */
    BufferedResult (*data)(const void *buffer, size_t size, void *ctx);

    bool (*is_empty)(void *ctx);

    bool (*is_full)(void *ctx);

    size_t (*available)(void *ctx);

    void (*consumed)(size_t nbytes, void *ctx);

    /**
     * The client asks to read more data.  The filter shall call
     * filtered_socket_internal_data() again.
     */
    bool (*read)(bool expect_more, void *ctx);

    /**
     * The client asks to write data to the socket.  The filter
     * processes it, and may then call
     * filtered_socket_internal_write().
     */
    ssize_t (*write)(const void *data, size_t length, void *ctx);

    /**
     * The client is willing to read, but does not expect it yet.  The
     * filter processes the call, and may then call
     * filtered_socket_internal_schedule_read().
     */
    void (*schedule_read)(bool expect_more, const struct timeval *timeout,
                          void *ctx);

    /**
     * The client wants to be called back as soon as writing becomes
     * possible.  The filter processes the call, and may then call
     * filtered_socket_internal_schedule_write().
     */
    void (*schedule_write)(void *ctx);

    /**
     * The client is not anymore interested in writing.  The filter
     * processes the call, and may then call
     * filtered_socket_internal_unschedule_write().
     */
    void (*unschedule_write)(void *ctx);

    /**
     * The underlying socket is ready for writing.  The filter may try
     * calling filtered_socket_internal_write() again.
     *
     * This method must not destroy the socket.  If an error occurs,
     * it shall return false.
     */
    bool (*internal_write)(void *ctx);

    bool (*closed)(void *ctx);

    bool (*remaining)(size_t remaining, void *ctx);

    /**
     * The buffered_socket has run empty after the socket has been
     * closed.  The filter may call filtered_socket_invoke_end() as
     * soon as all its buffers have been consumed.
     */
    void (*end)(void *ctx);

    void (*close)(void *ctx);
};

/**
 * A wrapper for #buffered_socket that can filter input and output.
 */
struct FilteredSocket {
    BufferedSocket base;

#ifndef NDEBUG
    bool ended;
#endif

    /**
     * The actual filter.  If this is nullptr, then this object behaves
     * just like #buffered_socket.
     */
    const SocketFilter *filter;
    void *filter_ctx;

    const BufferedSocketHandler *handler;
    void *handler_ctx;

    /**
     * Is there still data in the filter's output?  Once this turns
     * from "false" to "true", the #buffered_socket_handler method
     * drained() will be invoked.
     */
    bool drained;

    void Init(struct pool &pool,
              int fd, enum istream_direct fd_type,
              const struct timeval *read_timeout,
              const struct timeval *write_timeout,
              const SocketFilter *filter, void *filter_ctx,
              const BufferedSocketHandler &handler, void *handler_ctx);

    bool HasFilter() const {
        return filter != nullptr;
    }

    enum istream_direct GetType() const {
        return filter == nullptr
            ? base.base.GetType()
            /* can't do splice() with a filter */
            : ISTREAM_NONE;
    }

    /**
     * Close the physical socket, but do not destroy the input buffer.  To
     * do the latter, call filtered_socket_destroy().
     */
    void Close() {
#ifndef NDEBUG
        /* work around bogus assertion failure */
        if (filter != nullptr && base.ended)
            return;
#endif

        base.Close();
    }

    /**
     * Just like Close(), but do not actually close the
     * socket.  The caller is responsible for closing the socket (or
     * scheduling it for reuse).
     */
    void Abandon() {
#ifndef NDEBUG
        /* work around bogus assertion failure */
        if (filter != nullptr && base.ended)
            return;
#endif

        base.Abandon();
    }

    /**
     * Destroy the object.  Prior to that, the socket must be removed
     * by calling either filtered_socket_close() or
     * filtered_socket_abandon().
     */
    void Destroy();

    /**
     * Returns the socket descriptor and calls Abandon().  Returns -1
     * if the input buffer is not empty.
     */
    int AsFD() {
        return filter != nullptr
            ? -1
            : base.AsFD();
    }

    /**
     * Is the socket still connected?  This does not actually check
     * whether the socket is connected, just whether it is known to be
     * closed.
     */
    bool IsConnected() const {
#ifndef NDEBUG
        /* work around bogus assertion failure */
        if (filter != nullptr && base.ended)
            return false;
#endif

        return base.IsConnected();
    }

    /**
     * Is the object still usable?  The socket may be closed already, but
     * the input buffer may still have data.
     */
    bool IsValid() const {
        return base.IsValid();
    }

    /**
     * Accessor for #drained.
     */
    bool IsDrained() const {
        assert(IsValid());

        return drained;
    }

    /**
     * Is the input buffer empty?
     */
    gcc_pure
    bool IsEmpty() const;

    /**
     * Is the input buffer full?
     */
    gcc_pure
    bool IsFull() const;

    /**
     * Returns the number of bytes in the input buffer.
     */
    gcc_pure
    size_t GetAvailable() const;

    /**
     * Mark the specified number of bytes of the input buffer as
     * "consumed".  Call this in the data() method.  Note that this
     * method does not invalidate the buffer passed to data().  It may
     * be called repeatedly.
     */
    void Consumed(size_t nbytes);

    /**
     * Returns the istream_direct mask for splicing data into this socket.
     */
    enum istream_direct GetDirectMask() const {
        return filter != nullptr
            ? ISTREAM_NONE
            : base.GetDirectMask();
    }

    /**
     * The caller wants to read more data from the socket.  There are
     * four possible outcomes: a call to filtered_socket_handler.read,
     * a call to filtered_socket_handler.direct, a call to
     * filtered_socket_handler.error or (if there is no data available
     * yet) an event gets scheduled and the function returns
     * immediately.
     */
    bool Read(bool expect_more);

    void SetCork(bool cork) {
        base.SetCork(cork);
    }

    ssize_t Write(const void *data, size_t length);

    ssize_t WriteFrom(int fd, enum istream_direct fd_type, size_t length) {
        assert(filter == nullptr);

        return base.WriteFrom(fd, fd_type, length);
    }

    gcc_pure
    bool IsReadyForWriting() const {
        assert(filter == nullptr);

        return base.IsReadyForWriting();
    }

    void ScheduleReadTimeout(bool expect_more, const struct timeval *timeout) {
        if (filter != nullptr && filter->schedule_read != nullptr)
            filter->schedule_read(expect_more, timeout, filter_ctx);
        else
            base.ScheduleReadTimeout(expect_more, timeout);
    }

    /**
     * Schedules reading on the socket with timeout disabled, to indicate
     * that you are willing to read, but do not expect it yet.  No direct
     * action is taken.  Use this to enable reading when you are still
     * sending the request.  When you are finished sending the request,
     * you should call filtered_socket_read() to enable the read timeout.
     */
    void ScheduleReadNoTimeout(bool expect_more) {
        ScheduleReadTimeout(expect_more, nullptr);
    }

    void ScheduleWrite() {
        if (filter != nullptr && filter->schedule_write != nullptr)
            filter->schedule_write(filter_ctx);
        else
            base.ScheduleWrite();
    }

    void UnscheduleWrite() {
        if (filter != nullptr && filter->unschedule_write != nullptr)
            filter->unschedule_write(filter_ctx);
        else
            base.UnscheduleWrite();
    }

    gcc_pure
    bool InternalIsEmpty() const {
        assert(filter != nullptr);

        return base.IsEmpty();
    }

    gcc_pure
    bool InternalIsFull() const {
        assert(filter != nullptr);

        return base.IsFull();
    }

    gcc_pure
    size_t InternalGetAvailable() const {
        assert(filter != nullptr);

        return base.GetAvailable();
    }

    void InternalConsumed(size_t nbytes)
    {
        assert(filter != nullptr);

        base.Consumed(nbytes);
    }

    bool InternalRead(bool expect_more) {
        assert(filter != nullptr);

        return base.Read(expect_more);
    }

    ssize_t InternalWrite(const void *data, size_t length) {
        assert(filter != nullptr);

        return base.Write(data, length);
    }

    /**
     * A #SocketFilter must call this function whenever it adds data to
     * its output buffer (only if it implements such a buffer).
     */
    void InternalUndrained() {
        assert(filter != nullptr);
        assert(IsConnected());

        drained = false;
    }

    /**
     * A #SocketFilter must call this function whenever its output buffer
     * drains (only if it implements such a buffer).
     */
    bool InternalDrained();


    void InternalScheduleRead(bool expect_more,
                              const struct timeval *timeout) {
        assert(filter != nullptr);

        base.ScheduleReadTimeout(expect_more, timeout);
    }

    void InternalScheduleWrite() {
        assert(filter != nullptr);

        base.ScheduleWrite();
    }

    void InternalUnscheduleWrite() {
        assert(filter != nullptr);

        base.UnscheduleWrite();
    }

    BufferedResult InvokeData(const void *data, size_t size) {
        assert(filter != nullptr);

        return handler->data(data, size, handler_ctx);
    }

    bool InvokeClosed() {
        assert(filter != nullptr);

        return handler->closed(handler_ctx);
    }

    bool InvokeRemaining(size_t remaining) {
        assert(filter != nullptr);

        return handler->remaining == nullptr ||
            handler->remaining(remaining, handler_ctx);
    }

    void InvokeEnd() {
        assert(filter != nullptr);
        assert(!ended);
        assert(base.ended);

#ifndef NDEBUG
        ended = true;
#endif

        if (handler->end != nullptr)
            handler->end(handler_ctx);
    }

    bool InvokeWrite() {
        assert(filter != nullptr);

        return handler->write(handler_ctx);
    }

    bool InvokeTimeout() {
        assert(filter != nullptr);

        return handler->timeout(handler_ctx);
    }

    void InvokeError(GError *error) {
        assert(filter != nullptr);

        handler->error(error, handler_ctx);
    }
};

gcc_const
static inline GQuark
filtered_socket_quark(void)
{
    return g_quark_from_static_string("filtered_socket");
}

#endif
