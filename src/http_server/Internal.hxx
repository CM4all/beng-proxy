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

#ifndef __BENG_HTTP_SERVER_INTERNAL_H
#define __BENG_HTTP_SERVER_INTERNAL_H

#include "Error.hxx"
#include "http_server.hxx"
#include "http_body.hxx"
#include "fs/FilteredSocket.hxx"
#include "net/SocketProtocolError.hxx"
#include "net/SocketAddress.hxx"
#include "event/TimerEvent.hxx"
#include "event/DeferEvent.hxx"
#include "istream/Handler.hxx"
#include "istream/Pointer.hxx"
#include "http/Method.h"
#include "util/Cancellable.hxx"
#include "util/DestructObserver.hxx"
#include "util/Exception.hxx"

struct HttpServerConnection final
    : BufferedSocketHandler, IstreamHandler, DestructAnchor {

    enum class BucketResult {
        MORE,
        BLOCKING,
        DEPLETED,
        DESTROYED,
    };

    struct RequestBodyReader : HttpBodyReader {
        HttpServerConnection &connection;

        RequestBodyReader(struct pool &_pool,
                          HttpServerConnection &_connection)
            :HttpBodyReader(_pool),
             connection(_connection) {}

        /* virtual methods from class Istream */

        off_t _GetAvailable(bool partial) noexcept override;
        void _Read() noexcept override;
        void _Close() noexcept override;
    };

    struct pool *const pool;

    /* I/O */
    FilteredSocket socket;

    /**
     * Track the total time for idle periods plus receiving all
     * headers from the client.  Unlike the #filtered_socket read
     * timeout, it is not refreshed after receiving some header data.
     */
    TimerEvent idle_timeout;

    DeferEvent defer_read;

    enum http_server_score score = HTTP_SERVER_NEW;

    /* handler */
    HttpServerConnectionHandler *handler;

    /* info */

    const SocketAddress local_address, remote_address;

    const char *const local_host_and_port;
    const char *const remote_host;

    /* request */
    struct Request {
        enum {
            /** there is no request (yet); waiting for the request
                line */
            START,

            /** parsing request headers; waiting for empty line */
            HEADERS,

            /** reading the request body */
            BODY,

            /** the request has been consumed, and we are going to send the response */
            END
        } read_state = START;

#ifndef NDEBUG
        enum class BodyState {
            START,
            NONE,
            EMPTY,
            READING,
            CLOSED,
        } body_state = BodyState::START;
#endif

        /**
         * This flag is true if we are currently calling the HTTP
         * request handler.  During this period,
         * http_server_request_stream_read() does nothing, to prevent
         * recursion.
         */
        bool in_handler;

        /** did the client send an "Expect: 100-continue" header? */
        bool expect_100_continue;

        /** send a "417 Expectation Failed" response? */
        bool expect_failed;

        HttpServerRequest *request = nullptr;

        CancellablePointer cancel_ptr;

        uint64_t bytes_received = 0;
    } request;

    /** the request body reader; this variable is only valid if
        read_state==READ_BODY */
    RequestBodyReader *request_body_reader;

    /** the response; this struct is only valid if
        read_state==READ_BODY||read_state==READ_END */
    struct Response {
        bool want_write;

        /**
         * Are we currently waiting for all output buffers to be
         * drained, before we can close the socket?
         *
         * @see BufferedSocketHandler::drained
         * @see http_server_socket_drained()
         */
        bool pending_drained = false;

        http_status_t status;
        char status_buffer[64];
        char content_length_buffer[32];
        IstreamPointer istream;
        off_t length;

        uint64_t bytes_sent = 0;

        Response()
            :istream(nullptr) {}
    } response;

    bool date_header;

    /* connection settings */
    bool keep_alive;

    HttpServerConnection(struct pool &_pool,
                         EventLoop &_loop,
                         SocketDescriptor fd, FdType fd_type,
                         SocketFilterPtr &&filter,
                         SocketAddress _local_address,
                         SocketAddress _remote_address,
                         bool _date_header,
                         HttpServerConnectionHandler &_handler);

    ~HttpServerConnection() {
        defer_read.Cancel();
    }

    void Delete() noexcept;

    EventLoop &GetEventLoop() {
        return defer_read.GetEventLoop();
    }

    gcc_pure
    bool IsValid() const {
        return socket.IsValid() && socket.IsConnected();
    }

    void IdleTimeoutCallback() noexcept;

    void Log() noexcept;

    void OnDeferredRead() noexcept;

    /**
     * @return false if the connection has been closed
     */
    bool ParseRequestLine(const char *line, size_t length);

    /**
     * @return false if the connection has been closed
     */
    bool HeadersFinished();

    /**
     * @return false if the connection has been closed
     */
    bool HandleLine(const char *line, size_t length);

    BufferedResult FeedHeaders(const void *_data, size_t length);

    /**
     * @return false if the connection has been closed
     */
    bool SubmitRequest();

    /**
     * @return false if the connection has been closed
     */
    BufferedResult Feed(const void *data, size_t size);

    /**
     * Send data from the input buffer to the request body istream
     * handler.
     */
    BufferedResult FeedRequestBody(const void *data, size_t size);

    /**
     * Attempt a "direct" transfer of the request body.  Caller must
     * hold an additional pool reference.
     */
    DirectResult TryRequestBodyDirect(SocketDescriptor fd, FdType fd_type);

    /**
     * @return false if the connection has been closed
     */
    bool MaybeSend100Continue();

    void SetResponseIstream(UnusedIstreamPtr r);

    /**
     * To be called after the response istream has seen end-of-file,
     * and has been destroyed.
     *
     * @return false if the connection has been closed
     */
    bool ResponseIstreamFinished();

    void SubmitResponse(http_status_t status,
                        HttpHeaders &&headers,
                        UnusedIstreamPtr body);

    void ScheduleWrite() {
        response.want_write = true;
        socket.ScheduleWrite();
    }

    /**
     * @return false if the connection has been closed
     */
    bool TryWrite() noexcept;
    BucketResult TryWriteBuckets2();
    BucketResult TryWriteBuckets() noexcept;

    void CloseRequest() noexcept;

    void CloseSocket() noexcept;
    void DestroySocket() noexcept;

    /**
     * The last response on this connection is finished, and it should
     * be closed.
     */
    void Done() noexcept;

    /**
     * The peer has closed the socket.
     */
    void Cancel() noexcept;

    /**
     * A fatal error has occurred, and the connection should be closed
     * immediately, without sending any further information to the
     * client.  This invokes
     * HttpServerConnectionHandler::HttpConnectionError(), but not
     * HttpServerConnectionHandler::HttpConnectionClosed().
     */
    void Error(std::exception_ptr e) noexcept;

    void Error(const char *msg) noexcept;

    void SocketErrorErrno(const char *msg) noexcept;

    template<typename T>
    void SocketError(T &&t) noexcept {
        try {
            ThrowException(std::forward<T>(t));
        } catch (...) {
            Error(std::make_exception_ptr(HttpServerSocketError()));
        }
    }

    void SocketError(const char *msg) noexcept {
        SocketError(std::runtime_error(msg));
    }

    void ProtocolError(const char *msg) noexcept {
        Error(std::make_exception_ptr(SocketProtocolError(msg)));
    }

    /* virtual methods from class BufferedSocketHandler */
    BufferedResult OnBufferedData() override;
    DirectResult OnBufferedDirect(SocketDescriptor fd, FdType fd_type) override;
    bool OnBufferedClosed() noexcept override;
    bool OnBufferedWrite() override;
    bool OnBufferedDrained() noexcept override;
    void OnBufferedError(std::exception_ptr e) noexcept override;

    /* virtual methods from class IstreamHandler */
    size_t OnData(const void *data, size_t length) override;
    ssize_t OnDirect(FdType type, int fd, size_t max_length) override;
    void OnEof() noexcept override;
    void OnError(std::exception_ptr ep) noexcept override;
};

/**
 * The timeout of an idle connection (READ_START) up until request
 * headers are received.
 */
extern const Event::Duration http_server_idle_timeout;

/**
 * The timeout for reading more request data (READ_BODY).
 */
extern const Event::Duration http_server_read_timeout;

/**
 * The timeout for writing more response data (READ_BODY, READ_END).
 */
extern const Event::Duration http_server_write_timeout;

HttpServerRequest *
http_server_request_new(HttpServerConnection *connection,
                        http_method_t method,
                        StringView uri) noexcept;

#endif
