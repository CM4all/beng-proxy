/*
 * Call the translation server.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "translate.h"
#include "stock.h"
#include "tcp-stock.h"
#include "growing-buffer.h"
#include "processor.h"
#include "async.h"
#include "uri-address.h"
#include "abort-unref.h"
#include "gb-io.h"
#include "beng-proxy/translation.h"

#include <daemon/log.h>

#include <assert.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <event.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <stdlib.h>

enum packet_reader_result {
    PACKET_READER_EOF,
    PACKET_READER_ERROR,
    PACKET_READER_INCOMPLETE,
    PACKET_READER_SUCCESS
};

struct packet_reader {
    struct beng_translation_header header;
    size_t header_position;
    char *payload;
    size_t payload_position;
};

struct translate_client {
    pool_t pool;

    /** the socket connection to the translation server */
    int fd;

    struct stock_item *stock_item;

    /** events for the socket */
    struct event event;

    /** the marshalled translate request */
    struct growing_buffer *request;

    translate_callback_t callback;
    void *callback_ctx;

    struct packet_reader reader;
    struct translate_response response;

    /** the current resource address being edited */
    struct resource_address *resource_address;

    /** pointer to the tail of the transformation linked list */
    struct translate_transformation **transformation_tail;

    /** this asynchronous operation is the translate request; aborting
        it causes the request to be cancelled */
    struct async_operation async;

    struct async_operation_ref *async_ref;
};

static struct translate_response error = {
    .status = -1,
};


/**
 * Release resources held by this object: the event object, the socket
 * lease, and the pool reference.
 */
static void
translate_client_release(struct translate_client *client, bool reuse)
{
    assert(client != NULL);
    assert(client->fd >= 0);

    if (client->event.ev_events != 0)
        event_del(&client->event);
    client->fd = -1;
    stock_put(client->stock_item, !reuse);
    pool_unref(client->pool);
}

static void
translate_client_abort(struct translate_client *client)
{
    client->callback(&error, client->callback_ctx);
    translate_client_release(client, false);
}


/*
 * request marshalling
 *
 */

static void
write_packet(struct growing_buffer *gb, uint16_t command,
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

static struct growing_buffer *
marshal_request(pool_t pool, const struct translate_request *request)
{
    struct growing_buffer *gb;

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
static enum packet_reader_result
packet_reader_read(pool_t pool, struct packet_reader *reader, int fd)
{
    ssize_t nbytes;

    if (reader->header_position < sizeof(reader->header)) {
        char *p = (char*)&reader->header;

        nbytes = read(fd, p + reader->header_position,
                      sizeof(reader->header) - reader->header_position);
        if (nbytes < 0 && errno == EAGAIN)
            return PACKET_READER_INCOMPLETE;

        if (nbytes <= 0)
            return (int)nbytes;

        reader->header_position += (size_t)nbytes;
        if (reader->header_position < sizeof(reader->header))
            return PACKET_READER_INCOMPLETE;

        reader->payload_position = 0;

        if (reader->header.length == 0) {
            reader->payload = NULL;
            return PACKET_READER_SUCCESS;
        }

        reader->payload = p_malloc(pool, reader->header.length + 1);
    }

    assert(reader->payload_position < reader->header.length);

    nbytes = read(fd, reader->payload + reader->payload_position,
                  reader->header.length - reader->payload_position);
    if (nbytes == 0)
        return PACKET_READER_EOF;

    if (nbytes < 0) {
        if (errno == EAGAIN)
            return PACKET_READER_INCOMPLETE;
        else
            return PACKET_READER_ERROR;
    }

    reader->payload_position += (size_t)nbytes;
    if (reader->payload_position < reader->header.length)
        return PACKET_READER_INCOMPLETE;

    reader->payload[reader->header.length] = 0;
    return PACKET_READER_SUCCESS;
}


/*
 * receive response
 *
 */

static struct translate_transformation *
translate_add_transformation(struct translate_client *client)
{
    struct translate_transformation *transformation
        = p_malloc(client->pool, sizeof(*transformation));

    transformation->next = NULL;
    *client->transformation_tail = transformation;
    client->transformation_tail = &transformation->next;

    return transformation;
}

static bool
parse_address_string(struct sockaddr_in *sin, const char *p)
{
    int ret;
    const char *colon;
    char ip[32];
    int port = 80;

    colon = strchr(p, ':');
    if (colon != NULL) {
        if (colon >= p + sizeof(ip))
            /* too long */
            return false;

        memcpy(ip, p, colon - p);
        ip[colon - p] = 0;
        p = ip;

        port = atoi(colon + 1);
    }

    ret = inet_aton(p, &sin->sin_addr);
    if (!ret)
        return false;

    sin->sin_family = AF_INET;
    sin->sin_port = htons(port);
    memset(&sin->sin_zero, 0, sizeof(sin->sin_zero));
    return true;
}

static void
translate_handle_packet(struct translate_client *client,
                        unsigned command, const char *payload,
                        size_t payload_length)
{
    struct translate_transformation *transformation;

    if (command == TRANSLATE_BEGIN) {
        if (client->response.status != (http_status_t)-1) {
            daemon_log(1, "double BEGIN from translation server\n");
            translate_client_abort(client);
            return;
        }
    } else {
        if (client->response.status == (http_status_t)-1) {
            daemon_log(1, "no BEGIN from translation server\n");
            translate_client_abort(client);
            return;
        }
    }

    switch ((enum beng_translation_command)command) {
    case TRANSLATE_END:
        client->callback(&client->response, client->callback_ctx);
        translate_client_release(client, true);
        return;

    case TRANSLATE_BEGIN:
        memset(&client->response, 0, sizeof(client->response));
        client->resource_address = &client->response.address;
        client->transformation_tail = &client->response.transformation;
        break;

    case TRANSLATE_HOST:
    case TRANSLATE_URI:
    case TRANSLATE_PARAM:
    case TRANSLATE_REMOTE_HOST:
    case TRANSLATE_WIDGET_TYPE:
        daemon_log(2, "misplaced translate request packet\n");
        break;

    case TRANSLATE_STATUS:
        if (payload_length != 2) {
            daemon_log(1, "size mismatch in STATUS packet from translation server\n");
            translate_client_abort(client);
            return;
        }

        client->response.status = *(const uint16_t*)payload;
        break;

    case TRANSLATE_PATH:
        if (client->resource_address == NULL ||
            client->resource_address->type != RESOURCE_ADDRESS_NONE) {
            daemon_log(2, "misplaced TRANSLATE_PATH packet\n");
            break;
        }

        if (payload == NULL) {
            daemon_log(2, "malformed TRANSLATE_PATH packet\n");
            break;
        }

        client->resource_address->type = RESOURCE_ADDRESS_LOCAL;
        client->resource_address->u.local.path = payload;
        break;

    case TRANSLATE_PATH_INFO:
        if (client->resource_address == NULL ||
            client->resource_address->type != RESOURCE_ADDRESS_CGI) {
            if (client->resource_address == NULL ||
                client->resource_address->type != RESOURCE_ADDRESS_LOCAL)
                daemon_log(2, "misplaced TRANSLATE_PATH_INFO packet\n");
            break;
        }

        client->resource_address->u.cgi.path_info = payload;
        break;

    case TRANSLATE_SITE:
        client->response.site = payload;
        break;

    case TRANSLATE_CONTENT_TYPE:
        if (client->resource_address == NULL ||
            client->resource_address->type != RESOURCE_ADDRESS_LOCAL) {
            daemon_log(2, "misplaced TRANSLATE_CONTENT_TYPE packet\n");
            break;
        }

        client->resource_address->u.local.content_type = payload;
        break;

    case TRANSLATE_PROXY:
        if (client->resource_address == NULL ||
            client->resource_address->type != RESOURCE_ADDRESS_NONE) {
            daemon_log(2, "misplaced TRANSLATE_PROXY packet\n");
            break;
        }

        if (payload == NULL) {
            daemon_log(2, "malformed TRANSLATE_PROXY packet\n");
            break;
        }

        client->resource_address->type = RESOURCE_ADDRESS_HTTP;
        client->resource_address->u.http =
            uri_address_new(client->pool, payload);
        break;

    case TRANSLATE_REDIRECT:
        client->response.redirect = payload;
        break;

    case TRANSLATE_FILTER:
        transformation = translate_add_transformation(client);
        transformation->type = TRANSFORMATION_FILTER;
        transformation->u.filter.type = RESOURCE_ADDRESS_NONE;
        client->resource_address = &transformation->u.filter;
        break;

    case TRANSLATE_PROCESS:
        transformation = translate_add_transformation(client);
        transformation->type = TRANSFORMATION_PROCESS;
        transformation->u.processor.options = PROCESSOR_REWRITE_URL;
        transformation->u.processor.domain = NULL;
        break;

    case TRANSLATE_DOMAIN:
        if (client->response.transformation == NULL ||
            client->response.transformation->type != TRANSFORMATION_PROCESS) {
            daemon_log(2, "misplaced TRANSLATE_DOMAIN packet\n");
            break;
        }

        client->response.transformation->u.processor.domain = payload;
        break;

    case TRANSLATE_CONTAINER:
        if (client->response.transformation == NULL ||
            client->response.transformation->type != TRANSFORMATION_PROCESS) {
            daemon_log(2, "misplaced TRANSLATE_CONTAINER packet\n");
            break;
        }

        client->response.transformation->u.processor.options |= PROCESSOR_CONTAINER;
        break;

    case TRANSLATE_STATEFUL:
        client->response.stateful = true;
        break;

    case TRANSLATE_SESSION:
        client->response.session = payload;
        break;

    case TRANSLATE_USER:
        client->response.user = payload;
        break;

    case TRANSLATE_LANGUAGE:
        client->response.language = payload;
        break;

    case TRANSLATE_CGI:
        if (client->resource_address == NULL ||
            client->resource_address->type != RESOURCE_ADDRESS_NONE) {
            daemon_log(2, "misplaced TRANSLATE_CGI packet\n");
            break;
        }

        if (payload == NULL) {
            daemon_log(2, "malformed TRANSLATE_CGI packet\n");
            break;
        }

        client->resource_address->type = RESOURCE_ADDRESS_CGI;
        client->resource_address->u.cgi.path = payload;
        break;

    case TRANSLATE_FASTCGI:
        if (client->resource_address == NULL ||
            client->resource_address->type != RESOURCE_ADDRESS_NONE) {
            daemon_log(2, "misplaced TRANSLATE_FASTCGI packet\n");
            break;
        }

        if (payload == NULL) {
            daemon_log(2, "malformed TRANSLATE_FASTCGI packet\n");
            break;
        }

        client->resource_address->type = RESOURCE_ADDRESS_FASTCGI;
        client->resource_address->u.cgi.path = payload;
        break;

    case TRANSLATE_AJP:
        if (client->resource_address == NULL ||
            client->resource_address->type != RESOURCE_ADDRESS_NONE) {
            daemon_log(2, "misplaced TRANSLATE_AJP packet\n");
            break;
        }

        if (payload == NULL) {
            daemon_log(2, "malformed TRANSLATE_AJP packet\n");
            break;
        }

        client->resource_address->type = RESOURCE_ADDRESS_AJP;
        client->resource_address->u.http =
            uri_address_new(client->pool, payload);
        break;

    case TRANSLATE_JAILCGI:
        if (client->resource_address == NULL ||
            client->resource_address->type != RESOURCE_ADDRESS_CGI) {
            daemon_log(2, "misplaced TRANSLATE_JAILCGI packet\n");
            break;
        }

        client->resource_address->u.cgi.jail = true;
        break;

    case TRANSLATE_INTERPRETER:
        if (client->resource_address == NULL ||
            client->resource_address->type != RESOURCE_ADDRESS_CGI ||
            client->resource_address->u.cgi.interpreter != NULL) {
            daemon_log(2, "misplaced TRANSLATE_INTERPRETER packet\n");
            break;
        }

        client->resource_address->u.cgi.interpreter = payload;
        break;

    case TRANSLATE_ACTION:
        if (client->resource_address == NULL ||
            client->resource_address->type != RESOURCE_ADDRESS_CGI ||
            client->resource_address->u.cgi.action != NULL) {
            daemon_log(2, "misplaced TRANSLATE_ACTION packet\n");
            break;
        }

        client->resource_address->u.cgi.action = payload;
        break;

    case TRANSLATE_SCRIPT_NAME:
        if (client->resource_address == NULL ||
            client->resource_address->type != RESOURCE_ADDRESS_CGI ||
            client->resource_address->u.cgi.script_name != NULL) {
            daemon_log(2, "misplaced TRANSLATE_SCRIPT_NAME packet\n");
            break;
        }

        client->resource_address->u.cgi.script_name = payload;
        break;

    case TRANSLATE_DOCUMENT_ROOT:
        client->response.document_root = payload;
        break;

    case TRANSLATE_ADDRESS:
        if (client->resource_address->type != RESOURCE_ADDRESS_HTTP) {
            daemon_log(2, "misplaced TRANSLATE_ADDRESS packet\n");
            break;
        }

        if (payload_length < 2) {
            daemon_log(2, "malformed TRANSLATE_ADDRESS packet\n");
            break;
        }

        uri_address_add(client->resource_address->u.http,
                        (const struct sockaddr *)payload, payload_length);
        break;


    case TRANSLATE_ADDRESS_STRING:
        if (client->resource_address->type != RESOURCE_ADDRESS_HTTP) {
            daemon_log(2, "misplaced TRANSLATE_ADDRESS_STRING packet\n");
            break;
        }

        if (payload_length < 7) {
            daemon_log(2, "malformed TRANSLATE_ADDRESS_STRING packet\n");
            break;
        }

        {
            struct sockaddr_in sin;
            bool ret;

            ret = parse_address_string(&sin, payload);
            if (!ret) {
                daemon_log(2, "malformed TRANSLATE_ADDRESS_STRING packet\n");
                break;
            }

            uri_address_add(client->resource_address->u.http,
                            (const struct sockaddr *)&sin, sizeof(sin));
        }

        break;
    }
}

static void
translate_try_read(struct translate_client *client)
{
    do {
        switch (packet_reader_read(client->pool, &client->reader,
                                   client->fd)) {
        case PACKET_READER_INCOMPLETE: {
            struct timeval tv = {
                .tv_sec = 60,
                .tv_usec = 0,
            };

            event_add(&client->event, &tv);
            return;
        }

        case PACKET_READER_ERROR:
            daemon_log(1, "read error from translation server: %s\n",
                       strerror(errno));
            translate_client_abort(client);
            return;

        case PACKET_READER_EOF:
            daemon_log(1, "translation server aborted the connection\n");
            translate_client_abort(client);
            return;

        case PACKET_READER_SUCCESS:
            break;
        }

        translate_handle_packet(client,
                                client->reader.header.command,
                                client->reader.payload == NULL ? "" : client->reader.payload,
                                client->reader.header.length);
        packet_reader_init(&client->reader);
    } while (client->fd >= 0);
}

static void
translate_read_event_callback(int fd __attr_unused, short event, void *ctx)
{
    struct translate_client *client = ctx;

    if (event == EV_TIMEOUT) {
        daemon_log(1, "read timeout on translation server\n");
        translate_client_abort(client);
        return;
    }

    pool_ref(client->pool);
    translate_try_read(client);
    pool_unref(client->pool);
}


/*
 * send requests
 *
 */

static void
translate_try_write(struct translate_client *client)
{
    ssize_t nbytes;
    struct timeval tv = {
        .tv_sec = 10,
        .tv_usec = 0,
    };

    nbytes = write_from_gb(client->fd, client->request);
    assert(nbytes != -2);

    if (nbytes < 0) {
        daemon_log(1, "write error on translation server: %s\n",
                   strerror(errno));
        translate_client_abort(client);
        return;
    }

    if (nbytes == 0 && growing_buffer_empty(client->request)) {
        /* the buffer is empty, i.e. the request has been sent -
           start reading the response */
        packet_reader_init(&client->reader);

        pool_ref(client->pool);
        event_set(&client->event, client->fd, EV_READ|EV_TIMEOUT,
                  translate_read_event_callback, client);
        translate_try_read(client);
        pool_unref(client->pool);
        return;
    }

    event_add(&client->event, &tv);
}

static void
translate_write_event_callback(int fd __attr_unused, short event, void *ctx)
{
    struct translate_client *client = ctx;

    if (event == EV_TIMEOUT) {
        daemon_log(1, "write timeout on translation server\n");
        translate_client_abort(client);
        return;
    }

    translate_try_write(client);
}


/*
 * async operation
 *
 */

static struct translate_client *
async_to_translate_connection(struct async_operation *ao)
{
    return (struct translate_client*)(((char*)ao) - offsetof(struct translate_client, async));
}

static void
translate_connection_abort(struct async_operation *ao)
{
    struct translate_client *client = async_to_translate_connection(ao);

    translate_client_release(client, false);
}

static struct async_operation_class translate_operation = {
    .abort = translate_connection_abort,
};


/*
 * constructor
 *
 */

static void
translate_stock_callback(void *ctx, struct stock_item *item)
{
    struct translate_client *client = ctx;

    if (item == NULL) {
        client->callback(&error, client->callback_ctx);
        pool_unref(client->pool);
        return;
    }

    client->fd = tcp_stock_item_get(item);
    client->stock_item = item;
    client->response.status = (http_status_t)-1;

    async_init(&client->async, &translate_operation);
    async_ref_set(client->async_ref, &client->async);

    event_set(&client->event, client->fd, EV_WRITE|EV_TIMEOUT,
              translate_write_event_callback, client);
    translate_try_write(client);
}

void
translate(pool_t pool,
          struct hstock *tcp_stock, const char *socket_path,
          const struct translate_request *request,
          translate_callback_t callback,
          void *ctx,
          struct async_operation_ref *async_ref)
{
    struct translate_client *client;

    assert(pool != NULL);
    assert(tcp_stock != NULL);
    assert(socket_path != NULL);
    assert(request != NULL);
    assert(request->uri != NULL || request->widget_type != NULL);
    assert(callback != NULL);

    pool_ref(pool);

    client = p_malloc(pool, sizeof(*client));
    client->pool = pool;
    client->request = marshal_request(pool, request);
    client->callback = callback;
    client->callback_ctx = ctx;
    client->async_ref = async_ref;

    hstock_get(tcp_stock, socket_path, NULL,
               translate_stock_callback, client,
               async_unref_on_abort(pool, async_ref));
}
