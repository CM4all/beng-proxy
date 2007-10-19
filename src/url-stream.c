/*
 * High level HTTP client.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "url-stream.h"
#include "client-socket.h"
#include "http-client.h"
#include "compiler.h"

#include <daemon/log.h>

#include <assert.h>
#include <errno.h>
#include <string.h>
#include <netdb.h>

struct url_stream {
    pool_t pool;

    http_method_t method;
    const char *uri;
    growing_buffer_t headers;
    off_t content_length;
    istream_t body;

    client_socket_t client_socket;
    http_client_connection_t http;
    int got_response;

    /* callback */
    url_stream_callback_t callback;
    void *callback_ctx;
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

static void
url_stream_connection_idle(void *ctx)
{
    url_stream_t us = ctx;

    http_client_connection_close(us->http);
}

static void
url_stream_connection_free(void *ctx)
{
    url_stream_t us = ctx;

    us->http = NULL;

    /* self-destruct only after we provided a response to the
       callback */
    if (us->got_response)
        url_stream_close(us);
}

static const struct http_client_connection_handler url_stream_connection_handler = {
    .idle = url_stream_connection_idle,
    .free = url_stream_connection_free,
};

static void
url_stream_response_response(http_status_t status, strmap_t headers,
                             off_t content_length, istream_t body,
                             void *ctx)
{
    url_stream_t us = ctx;

    us->got_response = 1;
    us->callback(status, headers, content_length, body, us->callback_ctx);
}

static void
url_stream_response_free(void *ctx)
{
    url_stream_t us = ctx;

    if (!us->got_response)
        us->callback((http_status_t)0, NULL, 0, NULL, us->callback_ctx);
}

static const struct http_client_response_handler url_stream_response_handler = {
    .response = url_stream_response_response,
    .free = url_stream_response_free,
};

static void
url_stream_client_socket_callback(int fd, int err, void *ctx)
{
    url_stream_t us = ctx;

    client_socket_free(&us->client_socket);

    if (err == 0) {
        assert(fd >= 0);

        us->got_response = 0;
        us->http = http_client_connection_new(us->pool, fd,
                                              &url_stream_connection_handler, us);
        http_client_request(us->http, us->method, us->uri, us->headers,
                            us->content_length, us->body,
                            &url_stream_response_handler, us);
    } else {
        daemon_log(1, "failed to connect: %s\n", strerror(err));
        us->http = NULL;
        /* XXX */
    }
}

url_stream_t attr_malloc
url_stream_new(pool_t pool,
               http_method_t method, const char *url,
               growing_buffer_t headers,
               off_t content_length, istream_t body,
               url_stream_callback_t callback, void *ctx)
{
    url_stream_t us;
    int ret;
    const char *p, *slash, *host_and_port;
    struct addrinfo hints, *ai;

    assert(url != NULL);
    assert(callback != NULL);

#ifdef NDEBUG
    pool_ref(pool);
#else
    pool = pool_new_linear(pool, "url_stream", 4096);
#endif

    us = p_malloc(pool, sizeof(*us));
    us->pool = pool;
    us->method = method;
    us->headers = headers;
    us->content_length = content_length;
    us->body = body;
    us->client_socket = NULL;
    us->callback = callback;
    us->callback_ctx = ctx;

    if (memcmp(url, "http://", 7) != 0) {
        /* XXX */
        url_stream_close(us);
        return NULL;
    }

    p = url + 7;
    slash = strchr(p, '/');
    if (slash == NULL || slash == p) {
        /* XXX */
        url_stream_close(us);
        return NULL;
    }

    host_and_port = p_strndup(us->pool, p, slash - p);

    memset(&hints, 0, sizeof(hints));
    hints.ai_family = PF_INET;
    hints.ai_socktype = SOCK_STREAM;

    /* XXX make this asynchronous */
    ret = getaddrinfo_helper(host_and_port, 80, &hints, &ai);
    if (ret != 0) {
        daemon_log(1, "failed to resolve proxy host name\n");
        url_stream_close(us);
        return NULL;
    }

    us->uri = slash;

    ret = client_socket_new(us->pool,
                            ai->ai_addr, ai->ai_addrlen,
                            url_stream_client_socket_callback, us,
                            &us->client_socket);
    if (ret != 0) {
        daemon_log(1, "client_socket_new() failed: %s\n",
                   strerror(errno));
        url_stream_close(us);
        return NULL;
    }

    freeaddrinfo(ai);

    return us;
}

void
url_stream_close(url_stream_t us)
{
    pool_t pool;

    assert(us != NULL);
    assert(us->pool != NULL);

    if (us->client_socket != NULL) {
        client_socket_free(&us->client_socket);
        us->callback((http_status_t)0, NULL, 0, NULL, us->callback_ctx);
    } else if (us->http != NULL)
        http_client_connection_close(us->http);

    pool = us->pool;
    us->pool = NULL;
    pool_unref(pool);
}
