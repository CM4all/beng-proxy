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

static void
substitution_processor_input(void *ctx)
{
    struct substitution *s = ctx;

    http_client_response_read(s->http);
}

static void
substitution_processor_meta(const char *content_type, void *ctx)
{
    struct substitution *s = ctx;

    s->handler->meta(s, content_type);
}

static size_t
substitution_processor_output(const void *data, size_t length, void *ctx)
{
    struct substitution *s = ctx;
    void *dest;
    size_t max_length;

    dest = fifo_buffer_write(s->buffer, &max_length);
    if (dest == NULL)
        return 0;

    if (length > max_length)
        length = max_length;

    memcpy(dest, data, length);
    fifo_buffer_append(s->buffer, length);

    s->handler->output(s);

    return length;
}

static void
substitution_processor_output_finished(void *ctx)
{
    struct substitution *s = ctx;

    assert(s->processor != NULL);

    processor_free(&s->processor);

    s->handler->output(s);
}

static void
substitution_processor_free(void *ctx)
{
    struct substitution *s = ctx;

    /* XXX when the processor fails, it will close itself and invoke
       this callback */
    if (s->processor != NULL) {
        /* XXX */
    }
}

static struct processor_handler substitution_processor_handler = {
    .input = substitution_processor_input,
    .meta = substitution_processor_meta,
    .output = substitution_processor_output,
    .output_finished = substitution_processor_output_finished,
    .free = substitution_processor_free,
};

static size_t
substitution_response_body(struct http_client_response *response,
                           const void *buffer, size_t length)
{
    struct substitution *s = response->handler_ctx;

    /* XXX */
    if (s->processor == NULL) {
        void *dest;
        size_t max_length;

        dest = fifo_buffer_write(s->buffer, &max_length);
        if (dest == NULL)
            return 0;

        if (length > max_length)
            length = max_length;

        memcpy(dest, buffer, length);
        fifo_buffer_append(s->buffer, length);

        s->handler->output(s);

        return length;
    } else
        return processor_input(s->processor, buffer, length);
}

static void
substitution_response_finished(struct http_client_response *response)
{
    struct substitution *s = response->handler_ctx;

    s->response = NULL;
    s->response_finished = 1;

    if (s->processor == NULL) {
        s->handler->output(s);
    } else {
        processor_input_finished(s->processor);
        processor_output(s->processor);
    }
}

static void
substitution_response_free(struct http_client_response *response)
{
    struct substitution *s = response->handler_ctx;

    if (!s->response_finished) {
        /* abort the transfer */
        assert(response == s->response);
        s->response = NULL;
        /* XXX */
    }
}

static struct http_client_request_handler substitution_request_handler = {
    .response_body = substitution_response_body,
    .response_finished = substitution_response_finished,
    .free = substitution_response_free,
};

static void 
substitution_http_client_callback(struct http_client_response *response,
                                  void *ctx)
{
    struct substitution *s = ctx;
    const char *value;

    assert(s->response == NULL);

    if (response == NULL) {
        /* XXX */
        s->http = NULL;
        return;
    }

    assert(response->content_length >= 0);

    value = strmap_get(response->headers, "content-type");
    if (strncmp(value, "text/html", 9) == 0) {
        s->processor = processor_new(s->pool,
                                     &substitution_processor_handler, s);
        if (s->processor == NULL) {
            abort();
        }
    }

    response->handler = &substitution_request_handler;
    response->handler_ctx = s;

    if (s->processor == NULL) {
        /* XXX */
        abort();
    }
}

static void
substitution_client_socket_callback(int fd, int err, void *ctx)
{
    struct substitution *s = ctx;

    if (err == 0) {
        assert(fd >= 0);

        s->buffer = fifo_buffer_new(s->pool, 4096);

        s->response = NULL;
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
    assert(s->handler->meta != NULL);

    s->response_finished = 0;
    s->buffer = NULL;
    s->processor = NULL;

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

    s->handler->meta(s, "text/html");
}

void
substitution_close(struct substitution *s)
{
    assert(s != NULL);

    if (s->client_socket != NULL) {
        client_socket_free(&s->client_socket);
    } else if (s->http != NULL) {
        http_client_connection_close(s->http);
        assert(s->http == NULL);
        assert(s->response == NULL);
    }
}

size_t
substitution_output(struct substitution *s,
                    substitution_output_t callback, void *callback_ctx)
{
    const char *data;
    size_t length, nbytes;

    if (s->buffer == NULL)
        return 0;

    data = fifo_buffer_read(s->buffer, &length);
    if (data == NULL)
        return 0;

    nbytes = callback(data, length, callback_ctx);
    assert(nbytes <= length);

    fifo_buffer_consume(s->buffer, nbytes);
    return nbytes;
}

int
substitution_finished(const struct substitution *s)
{
    return s->processor == NULL && s->response_finished &&
        (s->buffer == NULL || fifo_buffer_empty(s->buffer));
}
