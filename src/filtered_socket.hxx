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
};

gcc_const
static inline GQuark
filtered_socket_quark(void)
{
    return g_quark_from_static_string("filtered_socket");
}

void
filtered_socket_init(FilteredSocket *s, struct pool *pool,
                     int fd, enum istream_direct fd_type,
                     const struct timeval *read_timeout,
                     const struct timeval *write_timeout,
                     const SocketFilter *filter,
                     void *filter_ctx,
                     const BufferedSocketHandler *handler,
                     void *handler_ctx);

static inline bool
filtered_socket_has_filter(const FilteredSocket *s)
{
    return s->filter != nullptr;
}

static inline enum istream_direct
filtered_socket_fd_type(const FilteredSocket *s)
{
    return s->filter == nullptr
        ? s->base.base.GetType()
        /* can't do splice() with a filter */
        : ISTREAM_NONE;
}

/**
 * Close the physical socket, but do not destroy the input buffer.  To
 * do the latter, call filtered_socket_destroy().
 */
static inline void
filtered_socket_close(FilteredSocket *s)
{
#ifndef NDEBUG
    /* work around bogus assertion failure */
    if (s->filter != nullptr && s->base.ended)
        return;
#endif

    s->base.Close();
}

/**
 * Just like filtered_socket_close(), but do not actually close the
 * socket.  The caller is responsible for closing the socket (or
 * scheduling it for reuse).
 */
static inline void
filtered_socket_abandon(FilteredSocket *s)
{
#ifndef NDEBUG
    /* work around bogus assertion failure */
    if (s->filter != nullptr && s->base.ended)
        return;
#endif

    s->base.Abandon();
}

/**
 * Destroy the object.  Prior to that, the socket must be removed by
 * calling either filtered_socket_close() or
 * filtered_socket_abandon().
 */
void
filtered_socket_destroy(FilteredSocket *s);

/**
 * Returns the socket descriptor and calls filtered_socket_abandon().
 * Returns -1 if the input buffer is not empty.
 */
static inline int
filtered_socket_as_fd(FilteredSocket *s)
{
    return s->filter != nullptr
        ? -1
        : s->base.AsFD();
}

/**
 * Is the socket still connected?  This does not actually check
 * whether the socket is connected, just whether it is known to be
 * closed.
 */
static inline bool
filtered_socket_connected(const FilteredSocket *s)
{
#ifndef NDEBUG
    /* work around bogus assertion failure */
    if (s->filter != nullptr && s->base.ended)
        return false;
#endif

    return s->base.IsConnected();
}

/**
 * Is the object still usable?  The socket may be closed already, but
 * the input buffer may still have data.
 */
static inline bool
filtered_socket_valid(const FilteredSocket *s)
{
    assert(s != nullptr);

    return s->base.IsValid();
}

/**
 * Accessor for #drained.
 */
static inline bool
filtered_socket_is_drained(const FilteredSocket *s)
{
    assert(s != nullptr);
    assert(filtered_socket_valid(s));

    return s->drained;

}

/**
 * Is the input buffer empty?
 */
gcc_pure
bool
filtered_socket_empty(const FilteredSocket *s);

/**
 * Is the input buffer full?
 */
gcc_pure
bool
filtered_socket_full(const FilteredSocket *s);

/**
 * Returns the number of bytes in the input buffer.
 */
gcc_pure
size_t
filtered_socket_available(const FilteredSocket *s);

/**
 * Mark the specified number of bytes of the input buffer as
 * "consumed".  Call this in the data() method.  Note that this method
 * does not invalidate the buffer passed to data().  It may be called
 * repeatedly.
 */
void
filtered_socket_consumed(FilteredSocket *s, size_t nbytes);

/**
 * Returns the istream_direct mask for splicing data into this socket.
 */
static inline enum istream_direct
filtered_socket_direct_mask(const FilteredSocket *s)
{
    assert(s != nullptr);

    return s->filter != nullptr
        ? ISTREAM_NONE
        : s->base.GetDirectMask();
}

/**
 * The caller wants to read more data from the socket.  There are four
 * possible outcomes: a call to filtered_socket_handler.read, a call
 * to filtered_socket_handler.direct, a call to
 * filtered_socket_handler.error or (if there is no data available
 * yet) an event gets scheduled and the function returns immediately.
 */
bool
filtered_socket_read(FilteredSocket *s, bool expect_more);

static inline void
filtered_socket_set_cork(FilteredSocket *s, bool cork)
{
    s->base.SetCork(cork);
}

ssize_t
filtered_socket_write(FilteredSocket *s,
                      const void *data, size_t length);

static inline ssize_t
filtered_socket_write_from(FilteredSocket *s,
                           int fd, enum istream_direct fd_type,
                           size_t length)
{
    assert(s->filter == nullptr);

    return s->base.WriteFrom(fd, fd_type, length);
}

gcc_pure
static inline bool
filtered_socket_ready_for_writing(const FilteredSocket *s)
{
    assert(s->filter == nullptr);

    return s->base.IsReadyForWriting();
}

static inline void
filtered_socket_schedule_read_timeout(FilteredSocket *s,
                                      bool expect_more,
                                      const struct timeval *timeout)
{
    if (s->filter != nullptr && s->filter->schedule_read != nullptr)
        s->filter->schedule_read(expect_more, timeout, s->filter_ctx);
    else
        s->base.ScheduleReadTimeout(expect_more, timeout);
}

/**
 * Schedules reading on the socket with timeout disabled, to indicate
 * that you are willing to read, but do not expect it yet.  No direct
 * action is taken.  Use this to enable reading when you are still
 * sending the request.  When you are finished sending the request,
 * you should call filtered_socket_read() to enable the read timeout.
 */
static inline void
filtered_socket_schedule_read_no_timeout(FilteredSocket *s,
                                         bool expect_more)
{
    filtered_socket_schedule_read_timeout(s, expect_more, nullptr);
}

static inline void
filtered_socket_schedule_write(FilteredSocket *s)
{
    if (s->filter != nullptr && s->filter->schedule_write != nullptr)
        s->filter->schedule_write(s->filter_ctx);
    else
        s->base.ScheduleWrite();
}

static inline void
filtered_socket_unschedule_write(FilteredSocket *s)
{
    if (s->filter != nullptr && s->filter->unschedule_write != nullptr)
        s->filter->unschedule_write(s->filter_ctx);
    else
        s->base.UnscheduleWrite();
}

gcc_pure
static inline bool
filtered_socket_internal_is_empty(const FilteredSocket *s)
{
    assert(s->filter != nullptr);

    return s->base.IsEmpty();
}

gcc_pure
static inline bool
filtered_socket_internal_is_full(const FilteredSocket *s)
{
    assert(s->filter != nullptr);

    return s->base.IsFull();
}

gcc_pure
static inline size_t
filtered_socket_internal_available(const FilteredSocket *s)
{
    assert(s->filter != nullptr);

    return s->base.GetAvailable();
}

static inline void
filtered_socket_internal_consumed(FilteredSocket *s, size_t nbytes)
{
    assert(s->filter != nullptr);

    s->base.Consumed(nbytes);
}

static inline bool
filtered_socket_internal_read(FilteredSocket *s, bool expect_more)
{
    assert(s->filter != nullptr);

    return s->base.Read(expect_more);
}

static inline ssize_t
filtered_socket_internal_write(FilteredSocket *s,
                               const void *data, size_t length)
{
    assert(s->filter != nullptr);

    return s->base.Write(data, length);
}

/**
 * A #SocketFilter must call this function whenever it adds data to
 * its output buffer (only if it implements such a buffer).
 */
static inline void
filtered_socket_internal_undrained(FilteredSocket *s)
{
    assert(s != nullptr);
    assert(s->filter != nullptr);
    assert(filtered_socket_connected(s));

    s->drained = false;
}

/**
 * A #SocketFilter must call this function whenever its output buffer
 * drains (only if it implements such a buffer).
 */
bool
filtered_socket_internal_drained(FilteredSocket *s);

static inline void
filtered_socket_internal_schedule_read(FilteredSocket *s,
                                       bool expect_more,
                                       const struct timeval *timeout)
{
    assert(s->filter != nullptr);

    s->base.ScheduleReadTimeout(expect_more, timeout);
}

static inline void
filtered_socket_internal_schedule_write(FilteredSocket *s)
{
    assert(s->filter != nullptr);

    s->base.ScheduleWrite();
}

static inline void
filtered_socket_internal_unschedule_write(FilteredSocket *s)
{
    assert(s->filter != nullptr);

    s->base.UnscheduleWrite();
}

static inline BufferedResult
filtered_socket_invoke_data(FilteredSocket *s,
                            const void *data, size_t size)
{
    assert(s->filter != nullptr);

    return s->handler->data(data, size, s->handler_ctx);
}

static inline bool
filtered_socket_invoke_closed(FilteredSocket *s)
{
    assert(s->filter != nullptr);

    return s->handler->closed(s->handler_ctx);
}

static inline bool
filtered_socket_invoke_remaining(FilteredSocket *s, size_t remaining)
{
    assert(s->filter != nullptr);

    return s->handler->remaining == nullptr ||
        s->handler->remaining(remaining, s->handler_ctx);
}

static inline void
filtered_socket_invoke_end(FilteredSocket *s)
{
    assert(s->filter != nullptr);
    assert(!s->ended);
    assert(s->base.ended);

#ifndef NDEBUG
    s->ended = true;
#endif

    if (s->handler->end != nullptr)
        s->handler->end(s->handler_ctx);
}

static inline bool
filtered_socket_invoke_write(FilteredSocket *s)
{
    assert(s->filter != nullptr);

    return s->handler->write(s->handler_ctx);
}

static inline bool
filtered_socket_invoke_timeout(FilteredSocket *s)
{
    assert(s->filter != nullptr);

    return s->handler->timeout(s->handler_ctx);
}

static inline void
filtered_socket_invoke_error(FilteredSocket *s, GError *error)
{
    assert(s->filter != nullptr);

    s->handler->error(error, s->handler_ctx);
}

#endif
