/*
 * Serve HTTP requests from the local VFS.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "connection.h"
#include "handler.h"
#include "client-socket.h"
#include "http-client.h"
#include "processor.h"

#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <netdb.h>

struct proxy_transfer {
    struct http_server_request *request;
    const char *uri;

    client_socket_t client_socket;
    http_client_connection_t http;
    off_t content_length;
    istream_t istream;
    int istream_eof;
};

static void
proxy_transfer_close(struct proxy_transfer *pt)
{
    if (pt->istream != NULL) {
        istream_t istream = pt->istream;
        pt->istream = NULL;
        istream_close(istream);
    }

    if (pt->http != NULL) {
        http_client_connection_close(pt->http);
        pt->http = NULL;
    }

    if (pt->request != NULL) {
        http_server_connection_free(&pt->request->connection);
        assert(pt->request == NULL);
    }
}

static size_t
proxy_client_istream_data(const void *data, size_t length, void *ctx)
{
    struct proxy_transfer *pt = ctx;

    /* XXX */
    if (pt->content_length == (off_t)-1)
        return http_server_send_chunk(pt->request->connection, data, length);
    else
        return http_server_send(pt->request->connection, data, length);
}

static void
proxy_client_istream_eof(void *ctx)
{
    struct proxy_transfer *pt = ctx;

    pt->istream = NULL;
    pt->istream_eof = 1;

    if (pt->request == NULL)
        return;

    if (pt->content_length == (off_t)-1)
        http_server_send_last_chunk(pt->request->connection);

    http_server_response_finish(pt->request->connection);
}

static void
proxy_client_istream_free(void *ctx)
{
    struct proxy_transfer *pt = ctx;

    if (!pt->istream_eof) {
        /* abort the transfer */
        pt->istream = NULL;
        proxy_transfer_close(pt);
    }
}

static const struct istream_handler proxy_client_istream_handler = {
    .data = proxy_client_istream_data,
    .eof = proxy_client_istream_eof,
    .free = proxy_client_istream_free,
};

static void 
proxy_http_client_callback(http_status_t status, strmap_t headers,
                           off_t content_length, istream_t body,
                           void *ctx)
{
    struct proxy_transfer *pt = ctx;
    const char *value;
    char response_headers[256];

    assert(pt->istream == NULL);

    if (status == 0) {
        pt->http = NULL;
        if (!pt->istream_eof)
            proxy_transfer_close(pt);
        return;
    }

    assert(content_length >= 0);

    pt->content_length = content_length;
    pt->istream = body;

    value = strmap_get(headers, "content-type");
    if (value != NULL && strncmp(value, "text/html", 9) == 0) {
        pool_ref(pt->request->pool);

        pt->content_length = (off_t)-1;
        pt->istream = processor_new(pt->request->pool, pt->istream);

        pool_unref(pt->request->pool);
        if (pt->istream == NULL) {
            /* XXX */
            abort();
        }

        snprintf(response_headers, sizeof(response_headers),
                 "Content-Type: %s\r\nTransfer-Encoding: chunked\r\n\r\n",
                 value);
    } else {
        snprintf(response_headers, sizeof(response_headers), "Content-Length: %lu\r\n\r\n",
                 (unsigned long)content_length);
    }

    assert(pt->istream->handler == NULL);

    pt->istream->handler = &proxy_client_istream_handler;
    pt->istream->handler_ctx = pt;

    http_server_send_status(pt->request->connection, 200);
    http_server_send(pt->request->connection, response_headers, strlen(response_headers));
    http_server_try_write(pt->request->connection);    
}

static const char *const copy_headers[] = {
    "user-agent",
    NULL
};

static void
proxy_client_forward_request(struct proxy_transfer *pt)
{
    strmap_t request_headers;
    const char *value;
    unsigned i;

    assert(pt != NULL);
    assert(pt->http != NULL);
    assert(pt->uri != NULL);

    request_headers = strmap_new(pt->request->pool, 64);

    for (i = 0; copy_headers[i] != NULL; ++i) {
        value = strmap_get(pt->request->headers, copy_headers[i]);
        if (value != NULL)
            strmap_addn(request_headers, copy_headers[i], value);
    }

    http_client_request(pt->http, HTTP_METHOD_GET, pt->uri, request_headers);
}

static void
proxy_client_socket_callback(int fd, int err, void *ctx)
{
    struct proxy_transfer *pt = ctx;

    if (err == 0) {
        assert(fd >= 0);

        pt->http = http_client_connection_new(pt->request->pool, fd,
                                              proxy_http_client_callback, pt);

        proxy_client_forward_request(pt);
    } else {
        fprintf(stderr, "failed to connect: %s\n", strerror(err));
        http_server_send_message(pt->request->connection,
                                 HTTP_STATUS_INTERNAL_SERVER_ERROR,
                                 "proxy connect failed");
        http_server_response_finish(pt->request->connection);
    }
}

static size_t proxy_response_body(struct http_server_request *request,
                                  void *buffer, size_t max_length)
{
    struct proxy_transfer *pt = request->handler_ctx;

    (void)buffer;
    (void)max_length;

    istream_read(pt->istream);

    return 0;
}

static void proxy_response_free(struct http_server_request *request)
{
    struct proxy_transfer *pt = request->handler_ctx;

    assert(request == pt->request);

    request->handler_ctx = NULL;
    pt->request = NULL;

    proxy_transfer_close(pt);
}

static const struct http_server_request_handler proxy_request_handler = {
    .response_body = proxy_response_body,
    .free = proxy_response_free,
};

static int
getaddrinfo_helper(const char *host_and_port, int default_port,
                   const struct addrinfo *hints,
                   struct addrinfo **aip) {
    const char *colon, *host, *port;
    char buffer[256];

    colon = strchr(host_and_port, ':');
    if (colon == NULL) {
        snprintf(buffer, sizeof(buffer), "%d", default_port);

        host = host_and_port;
        port = buffer;
    } else {
        size_t len = colon - host_and_port;

        if (len >= sizeof(buffer)) {
            errno = ENAMETOOLONG;
            return EAI_SYSTEM;
        }

        memcpy(buffer, host_and_port, len);
        buffer[len] = 0;

        host = buffer;
        port = colon + 1;
    }

    if (strcmp(host, "*") == 0)
        host = "0.0.0.0";

    return getaddrinfo(host, port, hints, aip);
}

void
proxy_callback(struct client_connection *connection,
               struct http_server_request *request,
               struct translated *translated)
{
    int ret;
    struct proxy_transfer *pt;
    const char *p, *slash, *host_and_port;
    struct addrinfo hints, *ai;

    (void)connection;

    if (request->method != HTTP_METHOD_HEAD &&
        request->method != HTTP_METHOD_GET) {
        http_server_send_message(request->connection,
                                 HTTP_STATUS_METHOD_NOT_ALLOWED,
                                 "This method is not supported.");
        http_server_response_finish(request->connection);
        return;
    }

    if (memcmp(translated->path, "http://", 7) != 0) {
        /* XXX */
        http_server_send_message(request->connection,
                                 HTTP_STATUS_BAD_REQUEST,
                                 "Invalid proxy URI");
        http_server_response_finish(request->connection);
        return;
    }

    p = translated->path + 7;
    slash = strchr(p, '/');
    if (slash == NULL || slash == p) {
        /* XXX */
        http_server_send_message(request->connection,
                                 HTTP_STATUS_BAD_REQUEST,
                                 "Invalid proxy URI");
        http_server_response_finish(request->connection);
        return;
    }

    host_and_port = p_strndup(request->pool, p, slash - p);

    memset(&hints, 0, sizeof(hints));
    hints.ai_family = PF_INET;
    hints.ai_socktype = SOCK_STREAM;

    /* XXX make this asynchronous */
    ret = getaddrinfo_helper(host_and_port, 80, &hints, &ai);
    if (ret != 0) {
        fprintf(stderr, "failed to resolve proxy host name\n");
        http_server_send_message(request->connection,
                                 HTTP_STATUS_INTERNAL_SERVER_ERROR,
                                 "Internal server error");
        http_server_response_finish(request->connection);
        return;
    }

    pt = p_calloc(request->pool, sizeof(*pt));
    pt->request = request;
    pt->uri = slash;

    ret = client_socket_new(request->pool,
                            ai->ai_addr, ai->ai_addrlen,
                            proxy_client_socket_callback, pt,
                            &pt->client_socket);
    if (ret != 0) {
        perror("client_socket_new() failed");
        freeaddrinfo(ai);
        http_server_send_message(request->connection,
                                 HTTP_STATUS_INTERNAL_SERVER_ERROR,
                                 "Internal server error");
        http_server_response_finish(request->connection);
        return;
    }

    freeaddrinfo(ai);

    request->handler = &proxy_request_handler;
    request->handler_ctx = pt;
}
