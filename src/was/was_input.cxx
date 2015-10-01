/*
 * Web Application Socket protocol, input data channel library.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "was_input.hxx"
#include "was_quark.h"
#include "event/Event.hxx"
#include "event/Callback.hxx"
#include "direct.hxx"
#include "istream/istream_internal.hxx"
#include "buffered_io.hxx"
#include "pool.hxx"
#include "fb_pool.hxx"
#include "SliceFifoBuffer.hxx"
#include "util/Cast.hxx"

#include <daemon/log.h>
#include <was/protocol.h>

#include <errno.h>
#include <string.h>

static constexpr struct timeval was_input_timeout = {
    .tv_sec = 120,
    .tv_usec = 0,
};

class WasInput {
public:
    struct istream output;

    int fd;
    Event event;

    const WasInputHandler *handler;
    void *handler_ctx;

    SliceFifoBuffer buffer;

    uint64_t received, guaranteed, length;

    bool closed, timeout, known_length;

    /**
     * Was this stream aborted prematurely?  In this case, the stream
     * is discarding the rest, and then calls the handler method
     * premature().  Only defined if known_length is true.
     */
    bool premature;

    void ScheduleRead() {
        assert(fd >= 0);
        assert(!buffer.IsDefined() || !buffer.IsFull());

        event.Add(timeout ? &was_input_timeout : nullptr);
    }

    void AbortError(GError *error) {
        event.Delete();

        /* protect against recursive was_input_free() call within the
           istream handler */
        closed = true;

        handler->abort(handler_ctx);

        istream_deinit_abort(&output, error);
    }

    void Eof() {
        assert(known_length);
        assert(received == length);

        event.Delete();

        if (premature) {
            handler->premature(handler_ctx);

            GError *error =
                g_error_new_literal(was_quark(), 0,
                                    "premature end of WAS response");
            istream_deinit_abort(&output, error);
        } else {
            handler->eof(handler_ctx);

            istream_deinit_eof(&output);
        }
    }

    bool CheckEof() {
        if (known_length && received >= length &&
            buffer.IsEmpty()) {
            Eof();
            return true;
        } else
            return false;
    }

    /**
     * Consume data from the input buffer.  Returns true if data has been
     * consumed.
     */
    bool SubmitBuffer() {
        auto r = buffer.Read();
        if (r.IsEmpty())
            return true;

        size_t nbytes = istream_invoke_data(&output, r.data, r.size);
        if (nbytes == 0)
            return false;

        buffer.Consume(nbytes);

        if (CheckEof())
            return false;

        buffer.FreeIfEmpty(fb_pool_get());
        return true;
    }

    /*
     * socket i/o
     *
     */

    bool TryBuffered();
    bool TryDirect();

    void TryRead() {
        if (istream_check_direct(&output, FdType::FD_PIPE)) {
            if (SubmitBuffer())
                TryDirect();
        } else {
            TryBuffered();
        }
    }

    void EventCallback(evutil_socket_t fd, short events);
};

inline bool
WasInput::TryBuffered()
{
    buffer.AllocateIfNull(fb_pool_get());

    size_t max_length = 4096;
    if (known_length) {
        uint64_t rest = length - received;
        if (rest < (uint64_t)max_length)
            max_length = rest;
    }

    ssize_t nbytes = read_to_buffer(fd, buffer, max_length);
    assert(nbytes != -2);

    if (nbytes == 0) {
        GError *error =
            g_error_new_literal(was_quark(), 0,
                                "server closed the data connection");
        AbortError(error);
        return false;
    }

    if (nbytes < 0) {
        if (errno == EAGAIN) {
            ScheduleRead();
            return true;
        }

        GError *error =
            g_error_new(was_quark(), 0,
                        "read error on data connection: %s",
                        strerror(errno));
        AbortError(error);
        return false;
    }

    received += nbytes;

    if (SubmitBuffer()) {
        assert(!buffer.IsDefinedAndFull());
        ScheduleRead();
    }

    return true;
}

inline bool
WasInput::TryDirect()
{
    assert(buffer.IsEmpty());

    size_t max_length = 0x1000000;
    if (known_length) {
        uint64_t rest = length - received;
        if (rest < (uint64_t)max_length)
            max_length = rest;
    }

    ssize_t nbytes = istream_invoke_direct(&output, FdType::FD_PIPE,
                                           fd, max_length);
    if (nbytes == ISTREAM_RESULT_EOF || nbytes == ISTREAM_RESULT_BLOCKING ||
        nbytes == ISTREAM_RESULT_CLOSED)
        return false;

    if (nbytes < 0) {
        if (errno == EAGAIN) {
            ScheduleRead();
            return false;
        }

        GError *error =
            g_error_new(was_quark(), 0,
                        "read error on data connection: %s",
                        strerror(errno));
        AbortError(error);
        return false;
    }

    received += nbytes;

    if (CheckEof())
        return false;

    ScheduleRead();
    return true;
}

/*
 * libevent callback
 *
 */

inline void
WasInput::EventCallback(gcc_unused evutil_socket_t _fd, short events)
{
    assert(_fd == fd);
    assert(fd >= 0);

    if (unlikely(events & EV_TIMEOUT)) {
        GError *error =
            g_error_new_literal(was_quark(), 0,
                                "data receive timeout");
        AbortError(error);
        return;
    }

    TryRead();

    pool_commit();
}


/*
 * istream implementation
 *
 */

static inline WasInput *
response_stream_to_data(struct istream *istream)
{
    return &ContainerCast2(*istream, &WasInput::output);
}

static off_t
was_input_istream_available(struct istream *istream, bool partial)
{
    WasInput *input = response_stream_to_data(istream);

    if (input->known_length)
        return input->length - input->received;
    else if (partial && input->guaranteed > input->received)
        return input->guaranteed - input->received;
    else
        return -1;
}

static void
was_input_istream_read(struct istream *istream)
{
    WasInput *input = response_stream_to_data(istream);

    input->event.Delete();

    if (input->SubmitBuffer())
        input->TryRead();
}

static void
was_input_istream_close(struct istream *istream)
{
    WasInput *input = response_stream_to_data(istream);

    input->event.Delete();

    /* protect against recursive was_input_free() call within the
       istream handler */
    input->closed = true;

    input->handler->abort(input->handler_ctx);

    istream_deinit(&input->output);
}

static const struct istream_class was_input_stream = {
    .available = was_input_istream_available,
    .read = was_input_istream_read,
    .close = was_input_istream_close,
};


/*
 * constructor
 *
 */

WasInput *
was_input_new(struct pool *pool, int fd,
              const WasInputHandler *handler, void *handler_ctx)
{
    assert(fd >= 0);
    assert(handler != nullptr);
    assert(handler->eof != nullptr);
    assert(handler->premature != nullptr);
    assert(handler->abort != nullptr);

    auto input = NewFromPool<WasInput>(*pool);
    istream_init(&input->output, &was_input_stream, pool);

    input->fd = fd;
    input->event.Set(input->fd, EV_READ|EV_TIMEOUT,
                     MakeEventCallback(WasInput, EventCallback), input);

    input->handler = handler;
    input->handler_ctx = handler_ctx;

    input->received = 0;
    input->guaranteed = 0;
    input->closed = false;
    input->timeout = false;
    input->known_length = false;

    return input;
}

void
was_input_free(WasInput *input, GError *error)
{
    assert(error != nullptr || input->closed);

    input->buffer.FreeIfDefined(fb_pool_get());

    input->event.Delete();

    if (!input->closed)
        istream_deinit_abort(&input->output, error);
    else if (error != nullptr)
        g_error_free(error);
}

void
was_input_free_unused(WasInput *input)
{
    assert(input->output.handler == nullptr);
    assert(!input->closed);

    istream_deinit(&input->output);
}

struct istream *
was_input_enable(WasInput *input)
{
    input->ScheduleRead();
    return &input->output;
}

bool
was_input_set_length(WasInput *input, uint64_t length)
{
    if (input->known_length) {
        if (length == input->length)
            return true;

        GError *error =
            g_error_new_literal(was_quark(), 0,
                                "wrong input length announced");
        input->AbortError(error);
        return false;
    }

    input->length = length;
    input->known_length = true;
    input->premature = false;

    if (input->CheckEof())
        return false;

    return true;
}

bool
was_input_premature(WasInput *input, uint64_t length)
{
    if (input->known_length && length > input->length) {
        GError *error =
            g_error_new_literal(was_quark(), 0,
                                "announced premature length is too large");
        input->AbortError(error);
        return false;
    }

    if (input->guaranteed > length || input->received > length) {
        GError *error =
            g_error_new_literal(was_quark(), 0,
                                "announced premature length is too small");
        input->AbortError(error);
        return false;
    }

    input->guaranteed = input->length = length;
    input->known_length = true;
    input->premature = true;

    if (input->CheckEof())
        return false;

    return true;
}
