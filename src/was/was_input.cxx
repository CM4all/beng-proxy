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
#include "istream/istream_oo.hxx"
#include "buffered_io.hxx"
#include "pool.hxx"
#include "fb_pool.hxx"
#include "SliceFifoBuffer.hxx"
#include "util/Cast.hxx"
#include "gerrno.h"

#include <daemon/log.h>
#include <was/protocol.h>

#include <errno.h>
#include <string.h>

static constexpr struct timeval was_input_timeout = {
    .tv_sec = 120,
    .tv_usec = 0,
};

class WasInput final : public Istream {
public:
    const int fd;
    Event event;

    const WasInputHandler &handler;
    void *const handler_ctx;

    SliceFifoBuffer buffer;

    uint64_t received = 0, length;

    bool enabled = false;

    bool closed = false, timeout = false, known_length = false;

    /**
     * Was this stream aborted prematurely?  In this case, the stream
     * is discarding the rest, and then calls the handler method
     * premature().  Only defined if known_length is true.
     */
    bool premature;

    WasInput(struct pool &p, int _fd,
             const WasInputHandler &_handler, void *_handler_ctx)
        :Istream(p), fd(_fd),
         handler(_handler), handler_ctx(_handler_ctx) {
        event.Set(fd, EV_READ|EV_TIMEOUT,
                  MakeEventCallback(WasInput, EventCallback), this);
    }

    void Free(GError *error);

    Istream &Enable() {
        assert(!enabled);
        enabled = true;
        ScheduleRead();
        return *this;
    }

    bool SetLength(uint64_t _length);
    bool Premature(uint64_t _length);

    bool CanRelease() const {
        return known_length && received == length;
    }

    using Istream::HasHandler;
    using Istream::Destroy;
    using Istream::DestroyError;

    void ScheduleRead() {
        assert(fd >= 0);
        assert(!buffer.IsDefined() || !buffer.IsFull());

        event.Add(timeout ? &was_input_timeout : nullptr);
    }

    void AbortError(GError *error) {
        buffer.FreeIfDefined(fb_pool_get());
        event.Delete();

        /* protect against recursive Free() call within the istream
           handler */
        closed = true;

        handler.abort(handler_ctx);
        DestroyError(error);
    }

    void Eof() {
        assert(known_length);
        assert(received == length);
        assert(!buffer.IsDefined());

        event.Delete();

        if (premature) {
            handler.premature(handler_ctx);

            GError *error =
                g_error_new_literal(was_quark(), 0,
                                    "premature end of WAS response");
            DestroyError(error);
        } else {
            handler.eof(handler_ctx);
            DestroyEof();
        }
    }

    bool CheckEof() {
        if (CanRelease() && buffer.IsEmpty()) {
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
        if (!r.IsEmpty()) {
            size_t nbytes = InvokeData(r.data, r.size);
            if (nbytes == 0)
                return false;

            buffer.Consume(nbytes);
            buffer.FreeIfEmpty(fb_pool_get());
        }

        if (CheckEof())
            return false;

        return true;
    }

    /*
     * socket i/o
     *
     */

    bool ReadToBuffer();
    bool TryBuffered();
    bool TryDirect();

    void TryRead() {
        if (CheckDirect(FdType::FD_PIPE)) {
            if (SubmitBuffer())
                TryDirect();
        } else {
            TryBuffered();
        }
    }

    void EventCallback(evutil_socket_t fd, short events);

    /* virtual methods from class Istream */

    off_t _GetAvailable(bool partial) override {
        if (known_length)
            return length - received + buffer.GetAvailable();
        else if (partial)
            return buffer.GetAvailable();
        else
            return -1;
    }

    void _Read() override {
        event.Delete();

        if (SubmitBuffer())
            TryRead();
    }

    void _Close() override {
        buffer.FreeIfDefined(fb_pool_get());
        event.Delete();

        /* protect against recursive Free() call within the istream
           handler */
        closed = true;

        handler.close(handler_ctx);

        Destroy();
    }
};

inline bool
WasInput::ReadToBuffer()
{
    buffer.AllocateIfNull(fb_pool_get());

    size_t max_length = 4096;
    if (known_length) {
        uint64_t rest = length - received;
        if (rest < (uint64_t)max_length)
            max_length = rest;

        if (max_length == 0)
            /* all the data we need is already in the buffer */
            return true;
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
        const int e = errno;

        if (e == EAGAIN) {
            buffer.FreeIfEmpty(fb_pool_get());
            ScheduleRead();
            return true;
        }

        AbortError(new_error_errno_msg2(e,
                                        "read error on WAS data connection"));
        return false;
    }

    received += nbytes;
    return true;
}

inline bool
WasInput::TryBuffered()
{
    if (!ReadToBuffer())
        return false;

    if (CanRelease() && handler.release != nullptr)
        handler.release(handler_ctx);

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
    assert(!buffer.IsDefined());

    size_t max_length = 0x1000000;
    if (known_length) {
        uint64_t rest = length - received;
        if (rest < (uint64_t)max_length)
            max_length = rest;
    }

    ssize_t nbytes = InvokeDirect(FdType::FD_PIPE, fd, max_length);
    if (nbytes == ISTREAM_RESULT_EOF || nbytes == ISTREAM_RESULT_BLOCKING ||
        nbytes == ISTREAM_RESULT_CLOSED)
        return false;

    if (nbytes < 0) {
        const int e = errno;

        if (e == EAGAIN) {
            ScheduleRead();
            return false;
        }

        AbortError(new_error_errno_msg2(e,
                                        "read error on WAS data connection"));
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

    return NewFromPool<WasInput>(*pool, *pool, fd,
                                 *handler, handler_ctx);
}

inline void
WasInput::Free(GError *error)
{
    assert(error != nullptr || closed || !enabled);

    buffer.FreeIfDefined(fb_pool_get());

    event.Delete();

    if (!closed && enabled)
        DestroyError(error);
    else if (error != nullptr)
        g_error_free(error);
}

void
was_input_free(WasInput *input, GError *error)
{
    input->Free(error);
}

void
was_input_free_unused(WasInput *input)
{
    assert(!input->HasHandler());
    assert(!input->closed);
    assert(!input->buffer.IsDefined());

    input->Destroy();
}

Istream &
was_input_enable(WasInput &input)
{
    return input.Enable();
}

inline bool
WasInput::SetLength(uint64_t _length)
{
    if (known_length) {
        if (_length == length)
            return true;

        // TODO: don't invoke Istream::DestroyError() if not yet enabled
        GError *error =
            g_error_new_literal(was_quark(), 0,
                                "wrong input length announced");
        AbortError(error);
        return false;
    }

    if (_length < received) {
        /* this length must be bogus, because we already received more than that from the socket */
        GError *error =
            g_error_new_literal(was_quark(), 0,
                                "announced length is too small");
        AbortError(error);
        return false;
    }

    length = _length;
    known_length = true;
    premature = false;

    if (received == length && handler.release != nullptr)
        handler.release(handler_ctx);

    if (enabled && CheckEof())
        return false;

    return true;
}

bool
was_input_set_length(WasInput *input, uint64_t length)
{
    return input->SetLength(length);
}

inline bool
WasInput::Premature(uint64_t _length)
{
    if (known_length && _length > length) {
        GError *error =
            g_error_new_literal(was_quark(), 0,
                                "announced premature length is too large");
        AbortError(error);
        return false;
    }

    if (_length < received) {
        GError *error =
            g_error_new_literal(was_quark(), 0,
                                "announced premature length is too small");
        AbortError(error);
        return false;
    }

    length = _length;
    known_length = true;
    premature = true;

    if (CheckEof())
        return false;

    return true;
}

bool
was_input_premature(WasInput *input, uint64_t length)
{
    return input->Premature(length);
}

bool
was_input_can_release(const WasInput &input)
{
    return input.CanRelease();
}
