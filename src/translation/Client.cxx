/*
 * Copyright 2007-2019 Content Management AG
 * All rights reserved.
 *
 * author: Max Kellermann <mk@cm4all.com>
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * - Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 *
 * - Redistributions in binary form must reproduce the above copyright
 * notice, this list of conditions and the following disclaimer in the
 * documentation and/or other materials provided with the
 * distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE
 * FOUNDATION OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED
 * OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "Client.hxx"
#include "Marshal.hxx"
#include "translation/Parser.hxx"
#include "translation/Request.hxx"
#include "translation/Response.hxx"
#include "translation/Handler.hxx"
#include "event/net/BufferedSocket.hxx"
#include "stopwatch.hxx"
#include "pool/pool.hxx"
#include "system/Error.hxx"
#include "util/Cancellable.hxx"
#include "util/Exception.hxx"
#include "lease.hxx"

#include <stdexcept>

#include <assert.h>
#include <string.h>
#include <errno.h>

static const uint8_t PROTOCOL_VERSION = 3;

class TranslateClient final : BufferedSocketHandler, Cancellable {
    struct pool &pool;

    const StopwatchPtr stopwatch;

    BufferedSocket socket;
    LeasePtr lease_ref;

    /** the marshalled translate request */
    GrowingBufferReader request;

    TranslateHandler &handler;

    TranslateParser parser;

public:
    TranslateClient(struct pool &p, EventLoop &event_loop,
                    StopwatchPtr &&_stopwatch,
                    SocketDescriptor fd, Lease &lease,
                    const TranslateRequest &request2,
                    GrowingBuffer &&_request,
                    TranslateHandler &_handler,
                    CancellablePointer &cancel_ptr) noexcept;

    bool TryWrite() noexcept;

private:
    void Destroy() noexcept {
        DeleteFromPool(pool, this);
    }

    void ReleaseSocket(bool reuse) noexcept;

    void Fail(std::exception_ptr ep) noexcept;

    BufferedResult Feed(const uint8_t *data, size_t length) noexcept;

    /* virtual methods from class BufferedSocketHandler */
    BufferedResult OnBufferedData() override {
        auto r = socket.ReadBuffer();
        assert(!r.empty());
        return Feed((const uint8_t *)r.data, r.size);
    }

    bool OnBufferedClosed() noexcept override {
        ReleaseSocket(false);
        return true;
    }

    bool OnBufferedWrite() override {
        return TryWrite();
    }

    void OnBufferedError(std::exception_ptr ep) noexcept override {
        Fail(NestException(ep,
                           std::runtime_error("Translation server connection failed")));
    }

    /* virtual methods from class Cancellable */
    void Cancel() noexcept override {
        stopwatch.RecordEvent("cancel");
        ReleaseSocket(false);
        Destroy();
    }
};

static constexpr auto translate_read_timeout = std::chrono::minutes(1);
static constexpr auto translate_write_timeout = std::chrono::seconds(10);

void
TranslateClient::ReleaseSocket(bool reuse) noexcept
{
    assert(socket.IsConnected());

    socket.Abandon();
    socket.Destroy();

    lease_ref.Release(reuse);
}

void
TranslateClient::Fail(std::exception_ptr ep) noexcept
{
    stopwatch.RecordEvent("error");

    ReleaseSocket(false);

    auto &_handler = handler;

    Destroy();

    _handler.OnTranslateError(ep);
}

/*
 * receive response
 *
 */

inline BufferedResult
TranslateClient::Feed(const uint8_t *data, size_t length) noexcept
try {
    size_t consumed = 0;
    while (consumed < length) {
        size_t nbytes = parser.Feed(data + consumed, length - consumed);
        if (nbytes == 0)
            /* need more data */
            break;

        consumed += nbytes;
        socket.DisposeConsumed(nbytes);

        auto result = parser.Process();
        switch (result) {
        case TranslateParser::Result::MORE:
            break;

        case TranslateParser::Result::DONE:
            ReleaseSocket(true);

            {
                auto &_handler = handler;
                auto &response = parser.GetResponse();
                Destroy();
                _handler.OnTranslateResponse(response);
            }

            return BufferedResult::CLOSED;
        }
    }

    return BufferedResult::MORE;
} catch (...) {
    Fail(std::current_exception());
    return BufferedResult::CLOSED;
}

/*
 * send requests
 *
 */

bool
TranslateClient::TryWrite() noexcept
{
    auto src = request.Read();
    assert(!src.IsNull());

    ssize_t nbytes = socket.Write(src.data, src.size);
    if (gcc_unlikely(nbytes < 0)) {
        if (gcc_likely(nbytes == WRITE_BLOCKING))
            return true;

        Fail(std::make_exception_ptr(MakeErrno("write error to translation server")));
        return false;
    }

    request.Consume(nbytes);
    if (request.IsEOF()) {
        /* the buffer is empty, i.e. the request has been sent */

        stopwatch.RecordEvent("request_end");

        socket.UnscheduleWrite();
        return socket.Read(true);
    }

    socket.ScheduleWrite();
    return true;
}

/*
 * constructor
 *
 */

inline
TranslateClient::TranslateClient(struct pool &p, EventLoop &event_loop,
                                 StopwatchPtr &&_stopwatch,
                                 SocketDescriptor fd, Lease &lease,
                                 const TranslateRequest &request2,
                                 GrowingBuffer &&_request,
                                 TranslateHandler &_handler,
                                 CancellablePointer &cancel_ptr) noexcept
    :pool(p),
     stopwatch(std::move(_stopwatch)),
     socket(event_loop), lease_ref(lease),
     request(std::move(_request)),
     handler(_handler),
     parser(p, request2, *NewFromPool<TranslateResponse>(p))
{
    socket.Init(fd, FdType::FD_SOCKET,
                translate_read_timeout,
                translate_write_timeout,
                *this);

    cancel_ptr = *this;
}

void
translate(struct pool &pool, EventLoop &event_loop,
          StopwatchPtr stopwatch,
          SocketDescriptor fd, Lease &lease,
          const TranslateRequest &request,
          TranslateHandler &handler,
          CancellablePointer &cancel_ptr) noexcept
try {
    assert(fd.IsDefined());
    assert(request.uri != nullptr || request.widget_type != nullptr ||
           request.pool != nullptr ||
           (!request.content_type_lookup.IsNull() &&
            request.suffix != nullptr));

    GrowingBuffer gb = MarshalTranslateRequest(PROTOCOL_VERSION,
                                               request);

    auto *client = NewFromPool<TranslateClient>(pool, pool, event_loop,
                                                std::move(stopwatch),
                                                fd, lease,
                                                request, std::move(gb),
                                                handler, cancel_ptr);

    client->TryWrite();
} catch (...) {
    lease.ReleaseLease(true);

    handler.OnTranslateError(std::current_exception());
}
