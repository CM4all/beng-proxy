/*
 * Copyright 2007-2017 Content Management AG
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
#include "Error.hxx"
#include "Protocol.hxx"
#include "Serialize.hxx"
#include "GrowingBuffer.hxx"
#include "http_response.hxx"
#include "istream_fcgi.hxx"
#include "istream_gb.hxx"
#include "istream/istream.hxx"
#include "istream/Pointer.hxx"
#include "istream/istream_cat.hxx"
#include "istream/Bucket.hxx"
#include "please.hxx"
#include "header_parser.hxx"
#include "direct.hxx"
#include "strmap.hxx"
#include "product.h"
#include "pool.hxx"
#include "system/Error.hxx"
#include "event/net/BufferedSocket.hxx"
#include "util/ByteOrder.hxx"
#include "util/Cast.hxx"
#include "util/ConstBuffer.hxx"
#include "util/StringUtil.hxx"
#include "util/ByteOrder.hxx"
#include "util/StringView.hxx"
#include "util/InstanceList.hxx"
#include "util/Cancellable.hxx"
#include "util/Exception.hxx"

#include <sys/socket.h>
#include <string.h>
#include <errno.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>

struct FcgiClient final
    : BufferedSocketHandler, Cancellable, Istream, IstreamHandler,
      WithInstanceList<FcgiClient> {

    BufferedSocket socket;

    struct lease_ref lease_ref;

    const int stderr_fd;

    HttpResponseHandler &handler;

    const uint16_t id;

    struct Request {
        IstreamPointer input;

        /**
         * This flag is set when the request istream has submitted
         * data.  It is used to check whether the request istream is
         * unavailable, to unschedule the socket write event.
         */
        bool got_data;

        Request():input(nullptr) {}
    } request;

    struct Response {
        enum {
            READ_HEADERS,

            /**
             * There is no response body.  Waiting for the
             * #FCGI_END_REQUEST packet, and then we'll forward the
             * response to the #http_response_handler.
             */
            READ_NO_BODY,

            READ_BODY,
        } read_state = READ_HEADERS;

        /**
         * Only used when read_state==READ_NO_BODY.
         */
        http_status_t status;

        StringMap headers;

        off_t available;

        /**
         * This flag is true in HEAD requests.  HEAD responses may
         * contain a Content-Length header, but no response body will
         * follow (RFC 2616 4.3).
         */
        const bool no_body;

        /**
         * This flag is true if SubmitResponse() is currently calling
         * the HTTP response handler.  During this period,
         * fcgi_client_response_body_read() does nothing, to prevent
         * recursion.
         */
        bool in_handler;

        /**
         * Is the FastCGI application currently sending a STDERR
         * packet?
         */
        bool stderr;

        Response(struct pool &p, bool _no_body)
            :headers(p), no_body(_no_body) {}
    } response;

    size_t content_length = 0, skip_length = 0;

    FcgiClient(struct pool &_pool, EventLoop &event_loop,
               SocketDescriptor fd, FdType fd_type, Lease &lease,
               int _stderr_fd,
               uint16_t _id, http_method_t method,
               HttpResponseHandler &_handler,
               CancellablePointer &cancel_ptr);

    ~FcgiClient();

    using Istream::GetPool;

    /**
     * Release the socket held by this object.
     */
    void ReleaseSocket(bool reuse) {
        socket.Abandon();
        p_lease_release(lease_ref, reuse, GetPool());
    }

    /**
     * Abort receiving the response status/headers from the FastCGI
     * server, and notify the HTTP response handler.
     */
    void AbortResponseHeaders(std::exception_ptr ep) noexcept;

    /**
     * Abort receiving the response body from the FastCGI server, and
     * notify the response body istream handler.
     */
    void AbortResponseBody(std::exception_ptr ep) noexcept;

    /**
     * Abort receiving the response from the FastCGI server.  This is
     * a wrapper for AbortResponseHeaders() or AbortResponseBody().
     */
    void AbortResponse(std::exception_ptr ep) noexcept;

    /**
     * Return type for AnalyseBuffer().
     */
    struct BufferAnalysis {
        /**
         * Offset of the end of the #FCGI_END_REQUEST packet, or 0 if
         * none was found.
         */
        size_t end_request_offset = 0;

        /**
         * Amount of #FCGI_STDOUT data found in the buffer.
         */
        size_t total_stdout = 0;
    };

    /**
     * Find the #FCGI_END_REQUEST packet matching the current request, and
     * returns the offset where it ends, or 0 if none was found.
     */
    gcc_pure
    BufferAnalysis AnalyseBuffer(const void *data, size_t size) const;

    bool HandleLine(const char *line, size_t length);

    size_t ParseHeaders(const char *data, size_t length);

    /**
     * Feed data into the FastCGI protocol parser.
     *
     * @return the number of bytes consumed, or 0 if this object has
     * been destructed
     */
    size_t Feed(const uint8_t *data, size_t length);

    /**
     * Submit the response metadata to the #HttpResponseHandler.
     *
     * @return false if the connection was closed
     */
    bool SubmitResponse();

    /**
     * Handle an END_REQUEST packet.  This function will always
     * destroy the client.
     */
    void HandleEnd();

    /**
     * A packet header was received.
     *
     * @return false if the client has been destroyed
     */
    bool HandleHeader(const struct fcgi_record_header &header);

    /**
     * Consume data from the input buffer.
     */
    BufferedResult ConsumeInput(const uint8_t *data, size_t length);

    /* virtual methods from class BufferedSocketHandler */
    BufferedResult OnBufferedData(const void *buffer, size_t size) override;
    bool OnBufferedClosed() override;
    bool OnBufferedRemaining(size_t remaining) override;
    bool OnBufferedWrite() override;
    bool OnBufferedTimeout() override;
    void OnBufferedError(std::exception_ptr e) override;

    /* virtual methods from class Cancellable */
    void Cancel() override;

    /* virtual methods from class Istream */

    off_t _GetAvailable(bool partial) override;
    void _Read() override;
    void _FillBucketList(IstreamBucketList &list) override;
    size_t _ConsumeBucketList(size_t nbytes) override;
    void _Close() noexcept override;

    /* virtual methods from class IstreamHandler */
    size_t OnData(const void *data, size_t length) override;
    ssize_t OnDirect(FdType type, int fd, size_t max_length) override;
    void OnEof() noexcept override;
    void OnError(std::exception_ptr ep) noexcept override;
};

static constexpr struct timeval fcgi_client_timeout = {
    .tv_sec = 120,
    .tv_usec = 0,
};

inline FcgiClient::~FcgiClient()
{
    socket.Destroy();

    if (stderr_fd >= 0)
        close(stderr_fd);
}

void
FcgiClient::AbortResponseHeaders(std::exception_ptr ep) noexcept
{
    assert(response.read_state == Response::READ_HEADERS ||
           response.read_state == Response::READ_NO_BODY);

    if (socket.IsConnected())
        ReleaseSocket(false);

    if (request.input.IsDefined())
        request.input.ClearAndClose();

    handler.InvokeError(ep);
    Destroy();
}

void
FcgiClient::AbortResponseBody(std::exception_ptr ep) noexcept
{
    assert(response.read_state == Response::READ_BODY);

    if (socket.IsConnected())
        ReleaseSocket(false);

    if (request.input.IsDefined())
        request.input.ClearAndClose();

    DestroyError(ep);
}

void
FcgiClient::AbortResponse(std::exception_ptr ep) noexcept
{
    assert(response.read_state == Response::READ_HEADERS ||
           response.read_state == Response::READ_NO_BODY ||
           response.read_state == Response::READ_BODY);

    if (response.read_state != Response::READ_BODY)
        AbortResponseHeaders(ep);
    else
        AbortResponseBody(ep);
}

void
FcgiClient::_Close() noexcept
{
    assert(response.read_state == Response::READ_BODY);

    if (socket.IsConnected())
        ReleaseSocket(false);

    if (request.input.IsDefined())
        request.input.ClearAndClose();

    Istream::_Close();
}

FcgiClient::BufferAnalysis
FcgiClient::AnalyseBuffer(const void *const _data0, size_t size) const
{
    const auto data0 = (const uint8_t *)_data0;
    const uint8_t *data = data0, *const end = data0 + size;

    FcgiClient::BufferAnalysis result;

    if (!response.stderr)
        result.total_stdout += content_length;

    /* skip the rest of the current packet */
    data += content_length + skip_length;

    while (true) {
        const struct fcgi_record_header *header =
            (const struct fcgi_record_header *)(const void *)data;
        data = (const uint8_t *)(header + 1);
        if (data > end)
            /* reached the end of the given buffer */
            break;

        data += FromBE16(header->content_length);
        data += header->padding_length;

        if (header->request_id == id) {
            if (header->type == FCGI_END_REQUEST) {
                /* found the END packet: stop here */
                result.end_request_offset = data - data0;
                break;
            } else if (header->type == FCGI_STDOUT) {
                result.total_stdout += FromBE16(header->content_length);
            }
        }
    }

    return result;
}

inline bool
FcgiClient::HandleLine(const char *line, size_t length)
{
    assert(line != nullptr);

    if (length > 0) {
        header_parse_line(GetPool(), response.headers, {line, length});
        return false;
    } else {
        response.read_state = Response::READ_BODY;
        response.stderr = false;
        return true;
    }
}

inline size_t
FcgiClient::ParseHeaders(const char *data, size_t length)
{
    const char *p = data, *const data_end = data + length;

    const char *next = nullptr;
    bool finished = false;

    const char *eol;
    while ((eol = (const char *)memchr(p, '\n', data_end - p)) != nullptr) {
        next = eol + 1;

        eol = StripRight(p, eol);

        finished = HandleLine(p, eol - p);
        if (finished)
            break;

        p = next;
    }

    return next != nullptr ? next - data : 0;
}

inline size_t
FcgiClient::Feed(const uint8_t *data, size_t length)
{
    if (response.stderr) {
        /* ignore errors and partial writes while forwarding STDERR
           payload; there's nothing useful we can do, and we can't let
           this delay/disturb the response delivery */
        if (stderr_fd >= 0)
            write(stderr_fd, data, length);
        else
            fwrite(data, 1, length, stderr);
        return length;
    }

    switch (response.read_state) {
        size_t consumed;

    case Response::READ_HEADERS:
        return ParseHeaders((const char *)data, length);

    case Response::READ_NO_BODY:
        /* unreachable */
        assert(false);
        return 0;

    case Response::READ_BODY:
        assert(response.available < 0 ||
               (off_t)length <= response.available);

        consumed = InvokeData(data, length);
        if (consumed > 0 && response.available >= 0) {
            assert((off_t)consumed <= response.available);
            response.available -= consumed;
        }

        return consumed;
    }

    /* unreachable */
    assert(false);
    return 0;
}

inline bool
FcgiClient::SubmitResponse()
{
    assert(response.read_state == Response::READ_BODY);

    http_status_t status = HTTP_STATUS_OK;

    const char *p = response.headers.Remove("status");
    if (p != nullptr) {
        int i = atoi(p);
        if (http_status_is_valid((http_status_t)i))
            status = (http_status_t)i;
    }

    if (http_status_is_empty(status) || response.no_body) {
        response.read_state = Response::READ_NO_BODY;
        response.status = status;

        /* ignore the rest of this STDOUT payload */
        skip_length += content_length;
        content_length = 0;
        return true;
    }

    response.available = -1;
    p = response.headers.Remove("content-length");
    if (p != nullptr) {
        char *endptr;
        unsigned long long l = strtoull(p, &endptr, 10);
        if (endptr > p && *endptr == 0)
            response.available = l;
    }

    response.in_handler = true;
    handler.InvokeResponse(status, std::move(response.headers), this);
    response.in_handler = false;

    return socket.IsValid();
}

inline void
FcgiClient::HandleEnd()
{
    assert(!socket.IsConnected());

    if (response.read_state == FcgiClient::Response::READ_HEADERS) {
        AbortResponseHeaders(std::make_exception_ptr(FcgiClientError("premature end of headers "
                                                                     "from FastCGI application")));
        return;
    }

    if (request.input.IsDefined())
        request.input.Close();

    if (response.read_state == FcgiClient::Response::READ_NO_BODY) {
        handler.InvokeResponse(response.status, std::move(response.headers),
                               nullptr);
        Destroy();
    } else if (response.available > 0) {
        AbortResponseBody(std::make_exception_ptr(FcgiClientError("premature end of body "
                                                                  "from FastCGI application")));
    } else
        DestroyEof();
}

inline bool
FcgiClient::HandleHeader(const struct fcgi_record_header &header)
{
    content_length = FromBE16(header.content_length);
    skip_length = header.padding_length;

    if (header.request_id != id) {
        /* wrong request id; discard this packet */
        skip_length += content_length;
        content_length = 0;
        return true;
    }

    switch (header.type) {
    case FCGI_STDOUT:
        response.stderr = false;

        if (response.read_state == FcgiClient::Response::READ_NO_BODY) {
            /* ignore all payloads until #FCGI_END_REQUEST */
            skip_length += content_length;
            content_length = 0;
        }

        return true;

    case FCGI_STDERR:
        response.stderr = true;
        return true;

    case FCGI_END_REQUEST:
        HandleEnd();
        return false;

    default:
        skip_length += content_length;
        content_length = 0;
        return true;
    }
}

inline BufferedResult
FcgiClient::ConsumeInput(const uint8_t *data0, size_t length0)
{
    const uint8_t *data = data0, *const end = data0 + length0;

    do {
        if (content_length > 0) {
            bool at_headers = response.read_state == FcgiClient::Response::READ_HEADERS;

            size_t length = end - data;
            if (length > content_length)
                length = content_length;

            if (response.read_state == FcgiClient::Response::READ_BODY &&
                response.available >= 0 &&
                (off_t)length > response.available) {
                /* the DATA packet was larger than the Content-Length
                   declaration - fail */
                AbortResponseBody(std::make_exception_ptr(FcgiClientError("excess data at end of body "
                                                                          "from FastCGI application")));
                return BufferedResult::CLOSED;
            }

            size_t nbytes = Feed(data, length);
            if (nbytes == 0) {
                if (at_headers) {
                    /* incomplete header line received, want more
                       data */
                    assert(response.read_state == FcgiClient::Response::READ_HEADERS);
                    assert(socket.IsValid());
                    return BufferedResult::MORE;
                }

                if (!socket.IsValid())
                    return BufferedResult::CLOSED;

                /* the response body handler blocks, wait for it to
                   become ready */
                return BufferedResult::BLOCKING;
            }

            data += nbytes;
            content_length -= nbytes;
            socket.Consumed(nbytes);

            if (at_headers && response.read_state == FcgiClient::Response::READ_BODY) {
                /* the read_state has been switched from HEADERS to
                   BODY: we have to deliver the response now */

                return SubmitResponse()
                    /* continue parsing the response body from the
                       buffer */
                    ? BufferedResult::AGAIN_EXPECT
                    : BufferedResult::CLOSED;
            }

            if (content_length > 0)
                return data < end && response.read_state != FcgiClient::Response::READ_HEADERS
                    /* some was consumed, try again later */
                    ? BufferedResult::PARTIAL
                    /* all input was consumed, want more */
                    : BufferedResult::MORE;

            continue;
        }

        if (skip_length > 0) {
            size_t nbytes = end - data;
            if (nbytes > skip_length)
                nbytes = skip_length;

            data += nbytes;
            skip_length -= nbytes;
            socket.Consumed(nbytes);

            if (skip_length > 0)
                return BufferedResult::MORE;

            continue;
        }

        const struct fcgi_record_header *header =
            (const struct fcgi_record_header *)(const void *)data;
        const size_t remaining = end - data;
        if (remaining < sizeof(*header))
            return BufferedResult::MORE;

        data += sizeof(*header);
        socket.Consumed(sizeof(*header));

        if (!HandleHeader(*header))
            return BufferedResult::CLOSED;
    } while (data != end);

    return BufferedResult::MORE;
}

/*
 * istream handler for the request
 *
 */

inline size_t
FcgiClient::OnData(const void *data, size_t length)
{
    assert(socket.IsConnected());
    assert(request.input.IsDefined());

    request.got_data = true;

    ssize_t nbytes = socket.Write(data, length);
    if (nbytes > 0)
        socket.ScheduleWrite();
    else if (gcc_likely(nbytes == WRITE_BLOCKING || nbytes == WRITE_DESTROYED))
        return 0;
    else if (nbytes < 0) {
        AbortResponse(NestException(std::make_exception_ptr(MakeErrno()),
                                    FcgiClientError("write to FastCGI application failed")));
        return 0;
    }

    return (size_t)nbytes;
}

inline ssize_t
FcgiClient::OnDirect(FdType type, int fd, size_t max_length)
{
    assert(socket.IsConnected());

    request.got_data = true;

    ssize_t nbytes = socket.WriteFrom(fd, type, max_length);
    if (gcc_likely(nbytes > 0))
        socket.ScheduleWrite();
    else if (nbytes == WRITE_BLOCKING)
        return ISTREAM_RESULT_BLOCKING;
    else if (nbytes == WRITE_DESTROYED)
        return ISTREAM_RESULT_CLOSED;
    else if (nbytes < 0 && errno == EAGAIN) {
        request.got_data = false;
        socket.UnscheduleWrite();
    }

    return nbytes;
}

void
FcgiClient::OnEof() noexcept
{
    assert(request.input.IsDefined());

    request.input.Clear();

    socket.UnscheduleWrite();
}

void
FcgiClient::OnError(std::exception_ptr ep) noexcept
{
    assert(request.input.IsDefined());

    request.input.Clear();

    AbortResponse(NestException(ep,
                                std::runtime_error("FastCGI request stream failed")));
}

/*
 * istream implementation for the response body
 *
 */

off_t
FcgiClient::_GetAvailable(bool partial)
{
    if (response.available >= 0)
        return response.available;

    const auto buffer = socket.ReadBuffer();
    if (buffer.size > content_length) {
        const auto analysis = AnalyseBuffer(buffer.data, buffer.size);
        if (analysis.end_request_offset > 0 || partial)
            return analysis.total_stdout;
    }

    return partial && !response.stderr ? (off_t)content_length : -1;
}

void
FcgiClient::_Read()
{
    if (response.in_handler)
        /* avoid recursion; the http_response_handler caller will
           continue parsing the response if possible */
        return;

    socket.Read(true);
}

void
FcgiClient::_FillBucketList(IstreamBucketList &list)
{
    if (response.available == 0)
        return;

    if (response.read_state != Response::READ_BODY || response.stderr) {
        list.SetMore();
        return;
    }

    auto b = socket.ReadBuffer();
    auto data = (const uint8_t *)b.data;
    const auto end = data + b.size;

    off_t available = response.available;
    size_t current_content_length = content_length;
    size_t current_skip_length = skip_length;

    bool found_end_request = false;

    while (true) {
        if (current_content_length > 0) {
            if (response.available >= 0 &&
                (off_t)current_content_length > response.available) {
                /* the DATA packet was larger than the Content-Length
                   declaration - fail */

                if (socket.IsConnected())
                    ReleaseSocket(false);

                if (request.input.IsDefined())
                    request.input.ClearAndClose();

                Destroy();

                throw FcgiClientError("excess data at end of body "
                                      "from FastCGI application");
            }

            const size_t remaining = end - data;
            size_t size = std::min(remaining, current_content_length);
            if (available > 0) {
                if ((off_t)size > available)
                    size = available;
                available -= size;
            }

            list.Push(ConstBuffer<void>(data, size));
            data += size;
            current_content_length -= size;

            if (current_content_length > 0)
                break;
        }

        if (current_skip_length > 0) {
            const size_t remaining = end - data;
            size_t size = std::min(remaining, current_skip_length);
            data += size;
            current_skip_length -= size;

            if (current_skip_length > 0)
                break;
        }

        const auto &header = *(const struct fcgi_record_header *)(const void *)data;
        const size_t remaining = end - data;
        if (remaining < sizeof(header))
            break;

        if (header.request_id != id) {
            /* ignore packets from other requests */
            current_skip_length = sizeof(header)
                + FromBE16(header.content_length)
                + header.padding_length;
            continue;
        }

        if (header.type != FCGI_STDOUT) {
            if (header.type == FCGI_END_REQUEST)
                found_end_request = true;

            break;
        }

        current_content_length = FromBE16(header.content_length);
        current_skip_length = header.padding_length;

        data += sizeof(header);
    }

    if (available > 0 || (available < 0 && !found_end_request))
        list.SetMore();
}

size_t
FcgiClient::_ConsumeBucketList(size_t nbytes)
{
    assert(response.available != 0);
    assert(response.read_state == Response::READ_BODY);
    assert(!response.stderr);

    size_t total = 0;

    while (nbytes > 0) {
        if (content_length > 0) {
            size_t consumed = std::min(nbytes, content_length);
            if (response.available > 0 && (off_t)consumed > response.available)
                consumed = response.available;

            socket.Consumed(consumed);
            content_length -= consumed;
            nbytes -= consumed;
            total += consumed;

            if (response.available > 0)
                response.available -= consumed;

            if (content_length > 0)
                break;
        }

        if (skip_length > 0) {
            const auto b = socket.ReadBuffer();
            if (b.empty())
                break;

            size_t consumed = std::min(b.size, skip_length);
            socket.Consumed(consumed);
            skip_length -= consumed;

            if (skip_length > 0)
                break;
        }

        const auto b = socket.ReadBuffer();
        const auto &header = *(const struct fcgi_record_header *)b.data;
        if (b.size < sizeof(header))
            break;

        if (header.request_id != id) {
            /* ignore packets from other requests */
            skip_length = sizeof(header) + FromBE16(header.content_length)
                + header.padding_length;
            continue;
        }

        if (header.type != FCGI_STDOUT)
            break;

        content_length = FromBE16(header.content_length);
        skip_length = header.padding_length;

        socket.Consumed(sizeof(header));
    }

    assert(nbytes == 0);

    if (total > 0)
        Consumed(total);

    return total;
}

/*
 * socket_wrapper handler
 *
 */

BufferedResult
FcgiClient::OnBufferedData(const void *buffer, size_t size)
{
    if (socket.IsConnected()) {
        /* check if the #FCGI_END_REQUEST packet can be found in the
           following data chunk */
        const auto analysis = AnalyseBuffer(buffer, size);
        if (analysis.end_request_offset > 0)
            /* found it: we no longer need the socket, everything we
               need is already in the given buffer */
            ReleaseSocket(analysis.end_request_offset == size);
    }

    const ScopePoolRef ref(GetPool() TRACE_ARGS);
    return ConsumeInput((const uint8_t *)buffer, size);
}

bool
FcgiClient::OnBufferedClosed()
{
    /* the rest of the response may already be in the input buffer */
    ReleaseSocket(false);
    return true;
}

bool
FcgiClient::OnBufferedRemaining(gcc_unused size_t remaining)
{
    /* only READ_BODY could have blocked */
    assert(response.read_state == Response::READ_BODY);

    /* the rest of the response may already be in the input buffer */
    return true;
}

bool
FcgiClient::OnBufferedWrite()
{
    const ScopePoolRef ref(GetPool() TRACE_ARGS);

    request.got_data = false;
    request.input.Read();

    const bool result = socket.IsValid();
    if (result && request.input.IsDefined()) {
        if (request.got_data)
            socket.ScheduleWrite();
        else
            socket.UnscheduleWrite();
    }

    return result;
}

bool
FcgiClient::OnBufferedTimeout()
{
    AbortResponse(std::make_exception_ptr(FcgiClientError("timeout")));
    return false;
}

void
FcgiClient::OnBufferedError(std::exception_ptr ep)
{
    AbortResponse(NestException(ep, FcgiClientError("FastCGI socket error")));
}

/*
 * async operation
 *
 */

void
FcgiClient::Cancel()
{
    /* Cancellable::Cancel() can only be used before the
       response was delivered to our callback */
    assert(response.read_state == Response::READ_HEADERS ||
           response.read_state == Response::READ_NO_BODY);
    assert(socket.IsConnected());

    ReleaseSocket(false);

    if (request.input.IsDefined())
        request.input.Close();

    Destroy();
}

/*
 * constructor
 *
 */

inline
FcgiClient::FcgiClient(struct pool &_pool, EventLoop &event_loop,
                       SocketDescriptor fd, FdType fd_type, Lease &lease,
                       int _stderr_fd,
                       uint16_t _id, http_method_t method,
                       HttpResponseHandler &_handler,
                       CancellablePointer &cancel_ptr)
    :Istream(_pool),
     socket(event_loop),
     stderr_fd(_stderr_fd),
     handler(_handler),
     id(_id),
     response(GetPool(), http_method_is_empty(method))
{
    socket.Init(fd, fd_type,
                &fcgi_client_timeout, &fcgi_client_timeout,
                *this);

    p_lease_ref_set(lease_ref, lease, GetPool(), "fcgi_client_lease");

    cancel_ptr = *this;
}

void
fcgi_client_request(struct pool *pool, EventLoop &event_loop,
                    SocketDescriptor fd, FdType fd_type, Lease &lease,
                    http_method_t method, const char *uri,
                    const char *script_filename,
                    const char *script_name, const char *path_info,
                    const char *query_string,
                    const char *document_root,
                    const char *remote_addr,
                    const StringMap &headers, Istream *body,
                    ConstBuffer<const char *> params,
                    int stderr_fd,
                    HttpResponseHandler &handler,
                    CancellablePointer &cancel_ptr)
{
    static unsigned next_request_id = 1;
    ++next_request_id;

    struct fcgi_record_header header = {
        .version = FCGI_VERSION_1,
        .type = FCGI_BEGIN_REQUEST,
        .request_id = ToBE16(next_request_id),
        .content_length = 0,
        .padding_length = 0,
        .reserved = 0,
    };
    static constexpr struct fcgi_begin_request begin_request = {
        .role = ToBE16(FCGI_RESPONDER),
        .flags = FCGI_KEEP_CONN,
    };

    assert(http_method_is_valid(method));

    auto client = NewFromPool<FcgiClient>(*pool, *pool, event_loop,
                                          fd, fd_type, lease,
                                          stderr_fd,
                                          header.request_id, method,
                                          handler, cancel_ptr);

    GrowingBuffer buffer;
    header.content_length = ToBE16(sizeof(begin_request));
    buffer.Write(&header, sizeof(header));
    buffer.Write(&begin_request, sizeof(begin_request));

    FcgiParamsSerializer ps(buffer, header.request_id);

    ps("REQUEST_METHOD", http_method_to_string(method))
        ("REQUEST_URI", uri)
        ("SCRIPT_FILENAME", script_filename)
        ("SCRIPT_NAME", script_name)
        ("PATH_INFO", path_info)
        ("QUERY_STRING", query_string)
        ("DOCUMENT_ROOT", document_root)
        ("SERVER_SOFTWARE", PRODUCT_TOKEN);

    if (remote_addr != nullptr)
        ps("REMOTE_ADDR", remote_addr);

    off_t available = body != nullptr
        ? body->GetAvailable(false)
        : -1;
    if (available >= 0) {
        char value[64];
        snprintf(value, sizeof(value),
                 "%lu", (unsigned long)available);

        const char *content_type = headers.Get("content-type");

        ps("HTTP_CONTENT_LENGTH", value)
            /* PHP wants the parameter without
               "HTTP_" */
            ("CONTENT_LENGTH", value);

        /* same for the "Content-Type" request
           header */
        if (content_type != nullptr)
            ps("CONTENT_TYPE", content_type);
    }

    if (!headers.IsEmpty()) {
        ps.Headers(headers);

        const char *https = headers.Get("x-cm4all-https");
        if (https != nullptr && strcmp(https, "on") == 0)
            ps("HTTPS", "on");
    }

    for (const StringView param : params) {
        const char *separator = param.Find('=');
        if (separator == nullptr)
            continue;

        StringView name(param.data, separator);
        StringView value(separator + 1, param.end());
        ps(name, value);
    }

    ps.Commit();

    header.type = FCGI_PARAMS;
    header.content_length = ToBE16(0);
    buffer.Write(&header, sizeof(header));

    Istream *request;

    if (body != nullptr)
        /* format the request body */
        request = istream_cat_new(*pool,
                                  istream_gb_new(*pool, std::move(buffer)),
                                  istream_fcgi_new(*pool, *body,
                                                   header.request_id));
    else {
        /* no request body - append an empty STDIN packet */
        header.type = FCGI_STDIN;
        header.content_length = ToBE16(0);
        buffer.Write(&header, sizeof(header));

        request = istream_gb_new(*pool, std::move(buffer));
    }

    client->request.input.Set(*request, *client,
                              istream_direct_mask_to(client->socket.GetType()));

    client->socket.ScheduleReadNoTimeout(true);
    client->request.input.Read();
}
