/*
 * Fill substitutions in a HTML stream, called by processor.c.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "substitution.h"

#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
#include <netdb.h>
#include <string.h>
#include <errno.h>

static size_t
substitution_istream_data(const void *data, size_t length, void *ctx)
{
    struct substitution *s = ctx;

    return s->handler->output(s, data, length);
}

static void
substitution_istream_eof(void *ctx)
{
    struct substitution *s = ctx;

    s->istream = NULL;
    s->istream_eof = 1;

    s->handler->eof(s);
}

static void
substitution_istream_free(void *ctx)
{
    struct substitution *s = ctx;

    if (!s->istream_eof) {
        /* abort the transfer */
        s->istream = NULL;
        /* XXX */
    }
}

static const struct istream_handler substitution_istream_handler = {
    .data = substitution_istream_data,
    .eof = substitution_istream_eof,
    .free = substitution_istream_free,
};

static void 
substitution_http_client_callback(http_status_t status, strmap_t headers,
                                  off_t content_length, istream_t body,
                                  void *ctx)
{
    struct substitution *s = ctx;
    const char *value;

    assert(s->istream == NULL);

    if (status == 0) {
        /* XXX */
        s->http = NULL;
        return;
    }

    assert(content_length >= 0);

    s->istream = body;

    value = strmap_get(headers, "content-type");
    if (value != NULL && strncmp(value, "text/html", 9) == 0) {
        s->istream = processor_new(s->pool, s->istream);
        if (s->istream == NULL) {
            abort();
        }
    }

    assert(s->istream->handler == NULL);

    s->istream->handler = &substitution_istream_handler;
    s->istream->handler_ctx = s;

    istream_read(s->istream);
}

static void
substitution_client_socket_callback(int fd, int err, void *ctx)
{
    struct substitution *s = ctx;

    if (err == 0) {
        assert(fd >= 0);

        s->istream = NULL;
        s->http = http_client_connection_new(s->pool, fd,
                                             substitution_http_client_callback, s);

        http_client_request(s->http, HTTP_METHOD_GET, s->uri, NULL);
    } else {
        fprintf(stderr, "failed to connect: %s\n", strerror(err));
        /* XXX */
    }
}

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
substitution_start(struct substitution *s)
{
    int ret;
    const char *p, *slash, *host_and_port;
    struct addrinfo hints, *ai;

    assert(s != NULL);
    assert(s->url != NULL);
    assert(s->handler != NULL);

    s->istream = NULL;
    s->istream_eof = 0;

    if (memcmp(s->url, "http://", 7) != 0) {
        /* XXX */
        return;
    }

    p = s->url + 7;
    slash = strchr(p, '/');
    if (slash == NULL || slash == p) {
        /* XXX */
        return;
    }

    host_and_port = p_strndup(s->pool, p, slash - p);

    memset(&hints, 0, sizeof(hints));
    hints.ai_family = PF_INET;
    hints.ai_socktype = SOCK_STREAM;

    /* XXX make this asynchronous */
    ret = getaddrinfo_helper(host_and_port, 80, &hints, &ai);
    if (ret != 0) {
        fprintf(stderr, "failed to resolve proxy host name\n");
        return;
    }

    s->uri = slash;

    ret = client_socket_new(s->pool,
                            ai->ai_addr, ai->ai_addrlen,
                            substitution_client_socket_callback, s,
                            &s->client_socket);
    if (ret != 0) {
        perror("client_socket_new() failed");
        return;
    }

    freeaddrinfo(ai);
}

void
substitution_close(struct substitution *s)
{
    assert(s != NULL);

    if (s->istream != NULL) {
        istream_close(s->istream);
        assert(s->istream == NULL);
    }

    if (s->client_socket != NULL) {
        client_socket_free(&s->client_socket);
    } else if (s->http != NULL) {
        http_client_connection_close(s->http);
        assert(s->http == NULL);
    }

    if (s->pool != NULL) {
        pool_t pool = s->pool;
        s->pool = NULL;
        pool_unref(pool);
    }
}

void
substitution_output(struct substitution *s)
{
    if (s->istream != NULL)
        istream_read(s->istream);
}
