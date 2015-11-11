/*
 * HTTP server implementation.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef __BENG_HTTP_SERVER_INTERNAL_H
#define __BENG_HTTP_SERVER_INTERNAL_H

#include "http_server.hxx"
#include "http_body.hxx"
#include "async.hxx"
#include "filtered_socket.hxx"
#include "net/SocketAddress.hxx"
#include "event/TimerEvent.hxx"
#include "istream/istream_pointer.hxx"

struct HttpServerConnection {
    enum class BucketResult {
        MORE,
        BLOCKING,
        DEPLETED,
        ERROR,
        DESTROYED,
    };

    struct RequestBodyReader : HttpBodyReader {
        HttpServerConnection &connection;

        RequestBodyReader(struct pool &_pool,
                          HttpServerConnection &_connection)
            :HttpBodyReader(_pool),
             connection(_connection) {}

        /* virtual methods from class Istream */

        off_t _GetAvailable(bool partial) override;
        void _Read() override;
        void _Close() override;
    };

    struct pool *pool;

    /* I/O */
    FilteredSocket socket;

    /**
     * Track the total time for idle periods plus receiving all
     * headers from the client.  Unlike the #filtered_socket read
     * timeout, it is not refreshed after receiving some header data.
     */
    TimerEvent idle_timeout;

    enum http_server_score score = HTTP_SERVER_NEW;

    /* handler */
    const HttpServerConnectionHandler *handler;
    void *handler_ctx;

    /* info */

    SocketAddress local_address, remote_address;

    const char *local_host_and_port;
    const char *remote_host_and_port, *remote_host;

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

        /**
         * This flag is true if we are currently calling the HTTP
         * request handler.  During this period,
         * http_server_request_stream_read() does nothing, to prevent
         * recursion.
         */
        bool in_handler;

        /** has the client sent a HTTP/1.0 request? */
        bool http_1_0;

        /** did the client send an "Expect: 100-continue" header? */
        bool expect_100_continue;

        /** send a "417 Expectation Failed" response? */
        bool expect_failed;

        struct http_server_request *request = nullptr;

        struct async_operation_ref async_ref;

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
                         int fd, FdType fd_type,
                         const SocketFilter *filter,
                         void *filter_ctx,
                         SocketAddress _local_address,
                         SocketAddress _remote_address,
                         bool _date_header,
                         const HttpServerConnectionHandler &_handler,
                         void *_handler_ctx);

    gcc_pure
    bool IsValid() const {
        return socket.IsValid() && socket.IsConnected();
    }

    void IdleTimeoutCallback();

    void Log();

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
    DirectResult TryRequestBodyDirect(int fd, FdType fd_type);

    /**
     * @return false if the connection has been closed
     */
    bool MaybeSend100Continue();

    void SetResponseIstream(Istream &r);

    /**
     * To be called after the response istream has seen end-of-file,
     * and has been destroyed.
     *
     * @return false if the connection has been closed
     */
    bool ResponseIstreamFinished();

    void SubmitResponse(http_status_t status,
                        HttpHeaders &&headers,
                        Istream *body);

    void ScheduleWrite() {
        response.want_write = true;
        socket.ScheduleWrite();
    }

    /**
     * @return false if the connection has been closed
     */
    bool TryWrite();
    BucketResult TryWriteBuckets2(GError **error_r);
    BucketResult TryWriteBuckets();

    void CloseRequest();

    void CloseSocket();
    void DestroySocket();

    /**
     * The last response on this connection is finished, and it should
     * be closed.
     */
    void Done();

    /**
     * The peer has closed the socket.
     */
    void Cancel();

    /**
     * A fatal error has occurred, and the connection should be closed
     * immediately, without sending any further information to the
     * client.  This invokes the error() handler method, but not
     * free().
     */
    void Error(GError *error);

    void Error(const char *msg);

    void ErrorErrno(const char *msg);

    /* response istream handler */
    size_t OnData(const void *data, size_t length);
    ssize_t OnDirect(FdType type, int fd, size_t max_length);
    void OnEof();
    void OnError(GError *error);
};

/**
 * The timeout of an idle connection (READ_START).
 */
extern const struct timeval http_server_idle_timeout;

/**
 * The total timeout of a client sending request headers.
 */
extern const struct timeval http_server_header_timeout;

/**
 * The timeout for reading more request data (READ_BODY).
 */
extern const struct timeval http_server_read_timeout;

/**
 * The timeout for writing more response data (READ_BODY, READ_END).
 */
extern const struct timeval http_server_write_timeout;

struct http_server_request *
http_server_request_new(HttpServerConnection *connection);

#endif
