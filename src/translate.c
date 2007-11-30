/*
 * Call the translation server.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "translate.h"
#include "stock.h"
#include "socket-util.h"
#include "growing-buffer.h"
#include "compiler.h"
#include "beng-proxy/translation.h"

#include <daemon/log.h>

#include <assert.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <event.h>

struct packet_reader {
    struct beng_translation_header header;
    size_t header_position;
    char *payload;
    size_t payload_position;
};

struct translate_connection {
    struct stock_item stock_item;

    pool_t pool;
    int fd;
    struct event event;
    growing_buffer_t request;

    translate_callback_t callback;
    void *ctx;

    struct packet_reader reader;
    struct translate_response response;
};

static struct translate_response error = {
    .status = -1,
};


/*
 * request marshalling
 *
 */

static void
write_packet(growing_buffer_t gb, uint16_t command,
             const char *payload)
{
    static struct beng_translation_header header;

    header.length = payload == NULL ? 0 : strlen(payload);
    header.command = command;

    growing_buffer_write_buffer(gb, &header, sizeof(header));
    if (header.length > 0)
        growing_buffer_write_buffer(gb, payload, header.length);
}

static growing_buffer_t
marshal_request(pool_t pool, const struct translate_request *request)
{
    growing_buffer_t gb;

    gb = growing_buffer_new(pool, 512);
    write_packet(gb, TRANSLATE_BEGIN, NULL);
    if (request->remote_host != NULL)
        write_packet(gb, TRANSLATE_REMOTE_HOST, request->remote_host);
    if (request->host != NULL)
        write_packet(gb, TRANSLATE_HOST, request->host);
    if (request->uri != NULL)
        write_packet(gb, TRANSLATE_URI, request->uri);
    if (request->session != NULL && *request->session != 0)
        write_packet(gb, TRANSLATE_SESSION, request->session);
    if (request->param != NULL)
        write_packet(gb, TRANSLATE_PARAM, request->param);
    write_packet(gb, TRANSLATE_END, NULL);

    return gb;
}


/*
 * packet reader
 *
 */

static void
packet_reader_init(struct packet_reader *reader)
{
    reader->header_position = 0;
}

/**
 * Read a packet from the socket.
 *
 * @return 0 on EOF, -1 on error, -2 when the packet is incomplete, 1 if the packet is complete
 */
static int
packet_reader_read(pool_t pool, struct packet_reader *reader, int fd)
{
    ssize_t nbytes;

    if (reader->header_position < sizeof(reader->header)) {
        char *p = (char*)&reader->header;

        nbytes = read(fd, p + reader->header_position,
                      sizeof(reader->header) - reader->header_position);
        if (nbytes < 0 && errno == EAGAIN)
            return -2;

        if (nbytes <= 0)
            return (int)nbytes;

        reader->header_position += (size_t)nbytes;
        if (reader->header_position < sizeof(reader->header))
            return -2;

        reader->payload_position = 0;

        if (reader->header.length == 0) {
            reader->payload = NULL;
            return 1;
        }

        reader->payload = p_malloc(pool, reader->header.length + 1);
    }

    assert(reader->payload_position < reader->header.length);

    nbytes = read(fd, reader->payload + reader->payload_position,
                  reader->header.length - reader->payload_position);
    if (nbytes < 0 && errno == EAGAIN)
        return -2;

    if (nbytes <= 0)
        return (int)nbytes;

    reader->payload_position += (size_t)nbytes;
    if (reader->payload_position < reader->header.length)
        return -2;

    reader->payload[reader->header.length] = 0;
    return 1;
}


/*
 * receive response
 *
 */

static void
translate_try_read(struct translate_connection *connection);

static void
translate_read_event_callback(int fd, short event, void *ctx)
{
    struct translate_connection *connection = ctx;

    if (event == EV_TIMEOUT) {
        daemon_log(1, "read timeout on translation server\n");
        close(fd);
        connection->callback(&error, connection->ctx);
        return;
    }

    pool_ref(connection->pool);
    translate_try_read(connection);
    pool_unref(connection->pool);
}

static void
translate_handle_packet(struct translate_connection *connection,
                        unsigned command, const char *payload,
                        size_t payload_length)
{
    if (command == TRANSLATE_BEGIN) {
        if (connection->response.status != (http_status_t)-1) {
            daemon_log(1, "double BEGIN from translation server\n");
            close(connection->fd);
            connection->fd = -1;
            connection->callback(&error, connection->ctx);
            return;
        }
    } else {
        if (connection->response.status == (http_status_t)-1) {
            daemon_log(1, "no BEGIN from translation server\n");
            close(connection->fd);
            connection->fd = -1;
            connection->callback(&error, connection->ctx);
            return;
        }
    }

    switch (command) {
    case TRANSLATE_END:
        connection->callback(&connection->response, connection->ctx);
        stock_put(&connection->stock_item, 0);
        connection->pool = NULL;
        break;

    case TRANSLATE_BEGIN:
        memset(&connection->response, 0, sizeof(connection->response));
        break;

    case TRANSLATE_STATUS:
        if (payload_length != 2) {
            daemon_log(1, "size mismatch in STATUS packet from translation server\n");
            close(connection->fd);
            connection->fd = -1;
            connection->callback(&error, connection->ctx);
            return;
        }

        connection->response.status = *(const uint16_t*)payload;
        break;

    case TRANSLATE_PATH:
        connection->response.path = payload;
        break;

    case TRANSLATE_PATH_INFO:
        connection->response.path_info = payload;
        break;

    case TRANSLATE_CONTENT_TYPE:
        connection->response.content_type = payload;
        break;

    case TRANSLATE_PROXY:
        connection->response.proxy = payload;
        break;

    case TRANSLATE_REDIRECT:
        connection->response.redirect = payload;
        break;

    case TRANSLATE_FILTER:
        connection->response.filter = payload;
        break;

    case TRANSLATE_PROCESS:
        connection->response.process = 1;
        break;

    case TRANSLATE_SESSION:
        connection->response.session = payload;
        break;

    case TRANSLATE_USER:
        connection->response.user = payload;
        break;

    case TRANSLATE_LANGUAGE:
        connection->response.language = payload;
        break;
    }
}

static void
translate_try_read(struct translate_connection *connection)
{
    int ret;

    do {
        ret = packet_reader_read(connection->pool, &connection->reader,
                                 connection->fd);
        if (ret == -2) {
            struct timeval tv = {
                .tv_sec = 60,
                .tv_usec = 0,
            };

            event_set(&connection->event, connection->fd, EV_READ|EV_TIMEOUT,
                      translate_read_event_callback, connection);
            event_add(&connection->event, &tv);
            return;
        } else if (ret == -1) {
            daemon_log(1, "read error from translation server: %s\n",
                       strerror(errno));
            close(connection->fd);
            connection->callback(&error, connection->ctx);
            return;
        } else if (ret == 0) {
            daemon_log(1, "translation server aborted the connection\n");
            close(connection->fd);
            connection->callback(&error, connection->ctx);
            return;
        }

        translate_handle_packet(connection,
                                connection->reader.header.command,
                                connection->reader.payload == NULL ? "" : connection->reader.payload,
                                connection->reader.header.length);
        packet_reader_init(&connection->reader);
    } while (connection->fd >= 0 && connection->pool != NULL);
}


/*
 * send requests
 *
 */

static void
translate_try_write(struct translate_connection *connection);

static void
translate_write_event_callback(int fd, short event, void *ctx)
{
    struct translate_connection *connection = ctx;

    if (event == EV_TIMEOUT) {
        daemon_log(1, "write timeout on translation server\n");
        close(fd);
        connection->callback(&error, connection->ctx);
        return;
    }

    translate_try_write(connection);
}

static void
translate_try_write(struct translate_connection *connection)
{
    const void *data;
    size_t length;
    ssize_t nbytes;
    struct timeval tv = {
        .tv_sec = 10,
        .tv_usec = 0,
    };

    data = growing_buffer_read(connection->request, &length);
    assert(data != NULL && length > 0);

    nbytes = write(connection->fd, data, length);
    if (nbytes < 0 && errno != EAGAIN) {
        daemon_log(1, "write error on translation server: %s\n",
                   strerror(errno));
        close(connection->fd);
        connection->callback(&error, connection->ctx);
        return;
    }

    if (nbytes > 0)
        growing_buffer_consume(connection->request, (size_t)nbytes);

    if ((size_t)nbytes == length) {
        data = growing_buffer_read(connection->request, &length);
        if (data == NULL) {
            /* the buffer is empty, i.e. the request has been sent -
               start reading the response */
            packet_reader_init(&connection->reader);

            pool_ref(connection->pool);
            translate_try_read(connection);
            pool_unref(connection->pool);
            return;
        }
    }

    event_set(&connection->event, connection->fd, EV_WRITE|EV_TIMEOUT,
              translate_write_event_callback, connection);
    event_add(&connection->event, &tv);
}


/*
 * stock class
 *
 */

static int
translate_stock_create(void *ctx, struct stock_item *item, const char *uri)
{
    struct translate_connection *connection = (struct translate_connection *)item;
    int ret;

    (void)ctx;

    connection->fd = socket_unix_connect(uri);
    if (connection->fd < 0) {
        daemon_log(1, "failed to connect to %s: %s\n",
                   uri, strerror(errno));
        return 0;
    }

    ret = socket_set_nonblock(connection->fd, 1);
    if (connection->fd < 0) {
        daemon_log(1, "failed to set non-blocking mode: %s\n",
                   strerror(errno));
        close(connection->fd);
        return 0;
    }

    return 1;
}

static int
translate_stock_validate(void *ctx, struct stock_item *item)
{
    struct translate_connection *connection = (struct translate_connection *)item;

    (void)ctx;

    return connection->fd >= 0;
}

static void
translate_stock_destroy(void *ctx, struct stock_item *item)
{
    struct translate_connection *connection = (struct translate_connection *)item;

    (void)ctx;

    if (connection->fd >= 0) {
        close(connection->fd);
        connection->fd = -1;
    }
}

static struct stock_class translate_stock_class = {
    .item_size = sizeof(struct translate_connection),
    .create = translate_stock_create,
    .validate = translate_stock_validate,
    .destroy = translate_stock_destroy,
};


/*
 * constructor
 *
 */

struct stock *
translate_stock_new(pool_t pool, const char *translation_socket)
{
    return stock_new(pool, &translate_stock_class, NULL, translation_socket);
}

void
translate(pool_t pool,
          struct stock *stock,
          const struct translate_request *request,
          translate_callback_t callback,
          void *ctx)
{
    struct translate_connection *connection;

    assert(pool != NULL);
    assert(request != NULL);
    assert(request->uri != NULL);
    assert(callback != NULL);

    if (stock == NULL) {
        callback(&error, ctx);
        return;
    }

    connection = (struct translate_connection *)stock_get(stock);
    if (connection == NULL) {
        callback(&error, ctx);
        return;
    }

    connection->pool = pool;
    connection->request = marshal_request(pool, request);
    connection->callback = callback;
    connection->ctx = ctx;
    connection->response.status = (http_status_t)-1;

    translate_try_write(connection);
}
