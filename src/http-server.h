/*
 * HTTP server implementation.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef __BENG_HTTP_SERVER_H
#define __BENG_HTTP_SERVER_H

#include "pool.h"
#include "istream.h"

#include <http/method.h>
#include <http/status.h>

struct sockaddr;
struct growing_buffer;
struct async_operation_ref;

struct http_server_connection;

/**
 * The score of a connection.  This is used under high load to
 * estimate which connections should be dropped first, as a remedy for
 * denial of service attacks.
 */
enum http_server_score {
    /**
     * Connection has been accepted, but client hasn't sent any data
     * yet.
     */
    HTTP_SERVER_NEW,

    /**
     * Client is transmitting the very first request.
     */
    HTTP_SERVER_FIRST,

    /**
     * At least one request was completed, but none was successful.
     */
    HTTP_SERVER_ERROR,

    /**
     * At least one request was completed successfully.
     */
    HTTP_SERVER_SUCCESS,
};

struct http_server_request {
    pool_t pool;
    struct http_server_connection *connection;

    const struct sockaddr *local_address;
    size_t local_address_length;

    /**
     * The local address (host and port) that was connected to.
     */
    const char *local_host;

    /**
     * The address (host and port) of the client.
     */
    const char *remote_address;

    /**
     * The address of the client, without the port number.
     */
    const char *remote_host;

    /* request metadata */
    http_method_t method;
    char *uri;
    struct strmap *headers;

    /**
     * The request body.  The handler is responsible for closing this
     * istream.
     */
    istream_t body;
};

struct http_server_connection_handler {
    void (*request)(struct http_server_request *request,
                    void *ctx,
                    struct async_operation_ref *async_ref);
    void (*log)(struct http_server_request *request,
                http_status_t status, off_t length,
                uint64_t bytes_received, uint64_t bytes_sent,
                void *ctx);

    /**
     * A fatal protocol level error has occurred, and the connection
     * was closed.
     *
     * This will be called instead of free().
     */
    void (*error)(GError *error, void *ctx);

    void (*free)(void *ctx);
};

G_GNUC_CONST
static inline GQuark
http_server_quark(void)
{
    return g_quark_from_static_string("http_server");
}

/**
 * @param date_header generate Date response headers?
 */
void
http_server_connection_new(pool_t pool, int fd, enum istream_direct fd_type,
                           const struct sockaddr *local_address,
                           size_t local_address_length,
                           const struct sockaddr *remote_address,
                           size_t remote_address_length,
                           bool date_header,
                           const struct http_server_connection_handler *handler,
                           void *ctx,
                           struct http_server_connection **connection_r);

void
http_server_connection_close(struct http_server_connection *connection);

void
http_server_connection_graceful(struct http_server_connection *connection);

enum http_server_score
http_server_connection_score(const struct http_server_connection *connection);

static inline bool
http_server_request_has_body(const struct http_server_request *request)
{
    return request->body != NULL;
}

void
http_server_response(const struct http_server_request *request,
                     http_status_t status,
                     struct growing_buffer *headers,
                     istream_t body);

void
http_server_send_message(const struct http_server_request *request,
                         http_status_t status, const char *msg);

void
http_server_send_redirect(const struct http_server_request *request,
                          http_status_t status, const char *location,
                          const char *msg);

#endif
