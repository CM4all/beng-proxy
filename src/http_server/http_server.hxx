/*
 * HTTP server implementation.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef __BENG_HTTP_SERVER_H
#define __BENG_HTTP_SERVER_H

#include "io/FdType.hxx"

#include <http/status.h>

#include <glib.h>

struct pool;
class EventLoop;
class Istream;
struct SocketFilter;
class SocketDescriptor;
class SocketAddress;
class HttpHeaders;

struct HttpServerRequest;
struct HttpServerConnection;
class HttpServerConnectionHandler;

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

G_GNUC_CONST
static inline GQuark
http_server_quark(void)
{
    return g_quark_from_static_string("http_server");
}

/**
 * @param date_header generate Date response headers?
 */
HttpServerConnection *
http_server_connection_new(struct pool *pool,
                           EventLoop &loop,
                           SocketDescriptor fd, FdType fd_type,
                           const SocketFilter *filter,
                           void *filter_ctx,
                           SocketAddress local_address,
                           SocketAddress remote_address,
                           bool date_header,
                           HttpServerConnectionHandler &handler);

void
http_server_connection_close(HttpServerConnection *connection);

void
http_server_connection_graceful(HttpServerConnection *connection);

enum http_server_score
http_server_connection_score(const HttpServerConnection *connection);

void
http_server_response(const HttpServerRequest *request,
                     http_status_t status,
                     HttpHeaders &&headers,
                     Istream *body);

/**
 * Generate a "simple" response with an optional plain-text body and
 * an optional "Location" redirect header.
 */
void
http_server_simple_response(const HttpServerRequest &request,
                            http_status_t status, const char *location,
                            const char *msg);

void
http_server_send_message(const HttpServerRequest *request,
                         http_status_t status, const char *msg);

void
http_server_send_redirect(const HttpServerRequest *request,
                          http_status_t status, const char *location,
                          const char *msg);

#endif
