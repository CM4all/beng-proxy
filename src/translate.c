/*
 * Call the translation server.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "translate.h"
#include "stock.h"
#include "socket-util.h"
#include "growing-buffer.h"
#include "processor.h"
#include "async.h"
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

struct translate_request_and_callback {
    pool_t pool;
    const struct translate_request *request;

    translate_callback_t callback;
    void *callback_ctx;

    struct async_operation_ref *async_ref;
};

struct translate_connection {
    struct stock_item stock_item;
    struct async_operation async;

    pool_t pool;
    int fd;
    struct event event;
    growing_buffer_t request;

    translate_callback_t callback;
    void *ctx;

    struct packet_reader reader;
    struct translate_response response;
    struct translate_transformation **transformation_tail;
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

    if (payload == NULL) {
        header.length = 0;
    } else {
        size_t length = strlen(payload);
        if (length > 0xffff)
            length = 0xffff;
        header.length = (uint16_t)length;
    }

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
    if (request->widget_type != NULL)
        write_packet(gb, TRANSLATE_WIDGET_TYPE, request->widget_type);
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
 * idle event
 */

static void
translate_idle_event_callback(int fd, short event, void *ctx)
{
    struct translate_connection *connection = ctx;
    unsigned char buffer;
    ssize_t nbytes;

    /* whatever happens here, we must close the translation server
       connection: this connection is idle, there is no request, but
       there is something on the socket */

    (void)event;

    nbytes = read(fd, &buffer, sizeof(buffer));
    if (nbytes < 0)
        daemon_log(1, "read error from translation server: %s\n",
                   strerror(errno));
    else if (nbytes == 0)
        daemon_log(3, "translation server closed the connection\n");
    else
        daemon_log(1, "translation server sent unrequested data\n");

    stock_del(&connection->stock_item);
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

    (void)fd;

    if (event == EV_TIMEOUT) {
        daemon_log(1, "read timeout on translation server\n");
        connection->callback(&error, connection->ctx);
        stock_put(&connection->stock_item, 1);
        return;
    }

    pool_ref(connection->stock_item.pool);
    translate_try_read(connection);
    pool_unref(connection->stock_item.pool);
}

static struct translate_transformation *
translate_add_transformation(struct translate_connection *connection)
{
    /* XXX wrong pool */
    struct translate_transformation *transformation
        = p_malloc(connection->pool, sizeof(*transformation));

    transformation->next = NULL;
    *connection->transformation_tail = transformation;
    connection->transformation_tail = &transformation->next;

    return transformation;
}

static void
translate_handle_packet(struct translate_connection *connection,
                        unsigned command, const char *payload,
                        size_t payload_length)
{
    struct translate_transformation *transformation;

    if (command == TRANSLATE_BEGIN) {
        if (connection->response.status != (http_status_t)-1) {
            daemon_log(1, "double BEGIN from translation server\n");
            connection->callback(&error, connection->ctx);
            stock_put(&connection->stock_item, 1);
            return;
        }
    } else {
        if (connection->response.status == (http_status_t)-1) {
            daemon_log(1, "no BEGIN from translation server\n");
            connection->callback(&error, connection->ctx);
            stock_put(&connection->stock_item, 1);
            return;
        }
    }

    switch (command) {
    case TRANSLATE_END:
        connection->callback(&connection->response, connection->ctx);
        stock_put(&connection->stock_item, 0);

        event_set(&connection->event, connection->fd, EV_READ,
                  translate_idle_event_callback, connection);
        event_add(&connection->event, NULL);
        break;

    case TRANSLATE_BEGIN:
        memset(&connection->response, 0, sizeof(connection->response));
        connection->transformation_tail= &connection->response.transformation;
        break;

    case TRANSLATE_STATUS:
        if (payload_length != 2) {
            daemon_log(1, "size mismatch in STATUS packet from translation server\n");
            connection->callback(&error, connection->ctx);
            stock_put(&connection->stock_item, 1);
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

    case TRANSLATE_SITE:
        connection->response.site = payload;
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
        if (payload != NULL) {
            /* XXX wrong pool */
            transformation = translate_add_transformation(connection);
            transformation->type = TRANSFORMATION_FILTER;
            transformation->u.filter = payload;
        }
        break;

    case TRANSLATE_PROCESS:
        /* XXX wrong pool */
        transformation = translate_add_transformation(connection);
        transformation->type = TRANSFORMATION_PROCESS;
        transformation->u.processor_options = 0;
        break;

    case TRANSLATE_CONTAINER:
        if (connection->response.transformation == NULL ||
            connection->response.transformation->type != TRANSFORMATION_PROCESS) {
            daemon_log(2, "misplaced TRANSLATE_CONTAINER packet\n");
            break;
        }

        connection->response.transformation->u.processor_options |= PROCESSOR_CONTAINER;
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

    case TRANSLATE_CGI:
        connection->response.cgi = 1;
        break;

    case TRANSLATE_DOCUMENT_ROOT:
        connection->response.document_root = payload;
        break;

    case TRANSLATE_ADDRESS:
        /* XXX ignore this packet for now */
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
            connection->callback(&error, connection->ctx);
            stock_put(&connection->stock_item, 1);
            return;
        } else if (ret == 0) {
            daemon_log(1, "translation server aborted the connection\n");
            connection->callback(&error, connection->ctx);
            stock_put(&connection->stock_item, 1);
            return;
        }

        translate_handle_packet(connection,
                                connection->reader.header.command,
                                connection->reader.payload == NULL ? "" : connection->reader.payload,
                                connection->reader.header.length);
        packet_reader_init(&connection->reader);
    } while (connection->fd >= 0 && !stock_item_is_idle(&connection->stock_item));
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

    (void)fd;

    if (event == EV_TIMEOUT) {
        daemon_log(1, "write timeout on translation server\n");
        connection->callback(&error, connection->ctx);
        stock_put(&connection->stock_item, 1);
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
        connection->callback(&error, connection->ctx);
        stock_put(&connection->stock_item, 1);
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

static void
translate_stock_create(void *ctx, struct stock_item *item, const char *uri,
                       struct async_operation_ref *async_ref __attr_unused)
{
    struct translate_connection *connection = (struct translate_connection *)item;
    int ret;

    (void)ctx;

    connection->event.ev_events = 0;

    connection->fd = socket_unix_connect(uri);
    if (connection->fd < 0) {
        daemon_log(1, "failed to connect to %s: %s\n",
                   uri, strerror(errno));
        stock_available(item, 0);
        return;
    }

    ret = socket_set_nonblock(connection->fd, 1);
    if (ret < 0) {
        daemon_log(1, "failed to set non-blocking mode: %s\n",
                   strerror(errno));
        stock_available(item, 0);
        return;
    }

    stock_available(item, 1);
    return;
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

    if (stock_item_is_idle(item) && connection->event.ev_events != 0)
        event_del(&connection->event);

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
 * async operation
 *
 */

static struct translate_connection *
async_to_translate_connection(struct async_operation *ao)
{
    return (struct translate_connection*)(((char*)ao) - offsetof(struct translate_connection, async));
}

static void
translate_connection_abort(struct async_operation *ao)
{
    struct translate_connection *connection = async_to_translate_connection(ao);

    assert(connection->fd >= 0);

    stock_put(&connection->stock_item, 1);
}

static struct async_operation_class translate_operation = {
    .abort = translate_connection_abort,
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

static void
translate_stock_callback(void *ctx, struct stock_item *item)
{
    struct translate_request_and_callback *request2 = ctx;
    struct translate_connection *connection = (struct translate_connection *)item;

    if (item == NULL) {
        request2->callback(&error, request2->callback_ctx);
        return;
    }

    if (connection->event.ev_events != 0)
        event_del(&connection->event);

    connection->pool = request2->pool;
    connection->request = marshal_request(connection->pool, request2->request);
    connection->callback = request2->callback;
    connection->ctx = request2->callback_ctx;
    connection->response.status = (http_status_t)-1;

    async_init(&connection->async, &translate_operation);
    async_ref_set(request2->async_ref, &connection->async);

    translate_try_write(connection);
}

void
translate(pool_t pool,
          struct stock *stock,
          const struct translate_request *request,
          translate_callback_t callback,
          void *ctx,
          struct async_operation_ref *async_ref)
{
    struct translate_request_and_callback *request2;

    assert(pool != NULL);
    assert(request != NULL);
    assert(request->uri != NULL || request->widget_type != NULL);
    assert(callback != NULL);

    if (stock == NULL) {
        callback(&error, ctx);
        return;
    }

    request2 = p_malloc(pool, sizeof(*request2));
    request2->pool = pool;
    request2->request = request;
    request2->callback = callback;
    request2->callback_ctx = ctx;
    request2->async_ref = async_ref;

    stock_get(stock, translate_stock_callback, request2, async_ref);
}
