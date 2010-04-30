/*
 * Call the translation server.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "translate.h"
#include "transformation.h"
#include "lease.h"
#include "growing-buffer.h"
#include "processor.h"
#include "async.h"
#include "uri-address.h"
#include "gb-io.h"
#include "strutil.h"
#include "strmap.h"
#include "stopwatch.h"
#include "beng-proxy/translation.h"
#include "pevent.h"

#include <daemon/log.h>
#include <socket/address.h>
#include <socket/resolver.h>

#include <glib.h>

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
#include <stdbool.h>
#include <netdb.h>

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

    struct stopwatch *stopwatch;

    int fd;
    struct lease_ref lease_ref;

    /** events for the socket */
    struct event event;

    /** the marshalled translate request */
    struct growing_buffer *request;

    translate_callback_t callback;
    void *callback_ctx;

    struct packet_reader reader;
    struct translate_response response;

    enum beng_translation_command previous_command;

    /** the current resource address being edited */
    struct resource_address *resource_address;

    /** pointer to the tail of the transformation view linked list */
    struct transformation_view **transformation_view_tail;

    /** the current transformation */
    struct transformation *transformation;

    /** pointer to the tail of the transformation linked list */
    struct transformation **transformation_tail;

    /** this asynchronous operation is the translate request; aborting
        it causes the request to be cancelled */
    struct async_operation async;
};

static const struct translate_response error = {
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

    stopwatch_dump(client->stopwatch);

    p_event_del(&client->event, client->pool);
    lease_release(&client->lease_ref, reuse);
    pool_unref(client->pool);
}

static void
translate_client_abort(struct translate_client *client)
{
    stopwatch_event(client->stopwatch, "error");

    client->callback(&error, client->callback_ctx);
    translate_client_release(client, false);
}


/*
 * request marshalling
 *
 */

static bool
write_packet_n(struct growing_buffer *gb, uint16_t command,
               const void *payload, size_t length)
{
    static struct beng_translation_header header;

    if (length >= 0xffff) {
        daemon_log(2, "payload for translate command %u too large\n",
                   command);
        return false;
    }

    header.length = (uint16_t)length;
    header.command = command;

    growing_buffer_write_buffer(gb, &header, sizeof(header));
    if (length > 0)
        growing_buffer_write_buffer(gb, payload, length);

    return true;
}

static bool
write_packet(struct growing_buffer *gb, uint16_t command,
             const char *payload)
{
    return write_packet_n(gb, command, payload,
                          payload != NULL ? strlen(payload) : 0);
}

/**
 * Forward the command to write_packet() only if #payload is not NULL.
 */
static bool
write_optional_packet(struct growing_buffer *gb, uint16_t command,
                      const char *payload)
{
    if (payload == NULL)
        return true;

    return write_packet(gb, command, payload);
}

static bool
write_sockaddr(struct growing_buffer *gb,
               uint16_t command, uint16_t command_string,
               const struct sockaddr *address, size_t address_length)
{
    char address_string[1024];

    assert(address != NULL);
    assert(address_length > 0);

    return write_packet_n(gb, command, address, address_length) &&
        (!socket_address_to_string(address_string, sizeof(address_string),
                                   address, address_length) ||
         write_packet(gb, command_string, address_string));
}

static bool
write_optional_sockaddr(struct growing_buffer *gb,
                        uint16_t command, uint16_t command_string,
                        const struct sockaddr *address, size_t address_length)
{
    assert((address == NULL) == (address_length == 0));

    return address != NULL
        ? write_sockaddr(gb, command, command_string, address, address_length)
        : true;
}

static struct growing_buffer *
marshal_request(pool_t pool, const struct translate_request *request)
{
    struct growing_buffer *gb;
    bool success;

    gb = growing_buffer_new(pool, 512);

    success = write_packet(gb, TRANSLATE_BEGIN, NULL) &&
        write_optional_sockaddr(gb, TRANSLATE_LOCAL_ADDRESS,
                                TRANSLATE_LOCAL_ADDRESS_STRING,
                                request->local_address,
                                request->local_address_length) &&
        write_optional_packet(gb, TRANSLATE_REMOTE_HOST,
                              request->remote_host) &&
        write_optional_packet(gb, TRANSLATE_HOST, request->host) &&
        write_optional_packet(gb, TRANSLATE_USER_AGENT, request->user_agent) &&
        write_optional_packet(gb, TRANSLATE_LANGUAGE,
                              request->accept_language) &&
        write_optional_packet(gb, TRANSLATE_AUTHORIZATION,
                              request->authorization) &&
        write_optional_packet(gb, TRANSLATE_URI, request->uri) &&
        write_optional_packet(gb, TRANSLATE_ARGS, request->args) &&
        write_optional_packet(gb, TRANSLATE_QUERY_STRING,
                              request->query_string) &&
        write_optional_packet(gb, TRANSLATE_WIDGET_TYPE,
                              request->widget_type) &&
        write_optional_packet(gb, TRANSLATE_SESSION, request->session) &&
        write_optional_packet(gb, TRANSLATE_PARAM, request->param) &&
        write_packet(gb, TRANSLATE_END, NULL);
    if (!success)
        return NULL;

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

        nbytes = recv(fd, p + reader->header_position,
                      sizeof(reader->header) - reader->header_position,
                      MSG_DONTWAIT);
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

    nbytes = recv(fd, reader->payload + reader->payload_position,
                  reader->header.length - reader->payload_position,
                  MSG_DONTWAIT);
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

static struct transformation *
translate_add_transformation(struct translate_client *client)
{
    struct transformation *transformation
        = p_malloc(client->pool, sizeof(*transformation));

    transformation->next = NULL;
    client->transformation = transformation;
    *client->transformation_tail = transformation;
    client->transformation_tail = &transformation->next;

    return transformation;
}

static bool
parse_address_string(struct uri_with_address *address, const char *p)
{
    struct addrinfo hints, *ai;
    int ret;

    memset(&hints, 0, sizeof(hints));
    hints.ai_flags = AI_NUMERICHOST;
    hints.ai_socktype = SOCK_STREAM;

    ret = socket_resolve_host_port(p, 80, &hints, &ai);
    if (ret != 0)
        return false;

    for (const struct addrinfo *i = ai; i != NULL; i = i->ai_next)
        uri_address_add(address, i->ai_addr, i->ai_addrlen);

    freeaddrinfo(ai);
    return true;
}

static bool
valid_view_name_char(char ch)
{
    return char_is_alphanumeric(ch) || ch == '_' || ch == '-';
}

static bool
valid_view_name(const char *name)
{
    assert(name != NULL);

    do {
        if (!valid_view_name_char(*name))
            return false;
    } while (*++name != 0);

    return true;
}

static void
add_view(struct translate_client *client, const char *name)
{
    struct transformation_view *view;

    if (!valid_view_name(name)) {
            daemon_log(1, "invalid view name\n");
            translate_client_abort(client);
            return;
    }

    view = p_malloc(client->pool, sizeof(*view));
    view->next = NULL;
    view->name = name;
    view->transformation = NULL;

    *client->transformation_view_tail = view;
    client->transformation_view_tail = &view->next;
    client->transformation_tail = &view->transformation;
    client->transformation = NULL;
}

static bool
parse_header_forward(struct header_forward_settings *settings,
                     const void *payload, size_t payload_length)
{
    const struct beng_header_forward_packet *packet = payload;

    if (payload_length % sizeof(*packet) != 0) {
        daemon_log(2, "malformed header forward packet\n");
        return false;
    }

    while (payload_length > 0) {
        if (packet->group < HEADER_GROUP_ALL ||
            packet->group >= HEADER_GROUP_MAX ||
            (packet->mode != HEADER_FORWARD_NO &&
             packet->mode != HEADER_FORWARD_YES &&
             packet->mode != HEADER_FORWARD_MANGLE) ||
            packet->reserved != 0) {
            daemon_log(2, "malformed header forward packet\n");
            return false;
        }

        if (packet->group == HEADER_GROUP_ALL)
            for (unsigned i = 0; i < HEADER_GROUP_MAX; ++i)
                settings->modes[i] = packet->mode;
        else
            settings->modes[packet->group] = packet->mode;

        ++packet;
        payload_length -= sizeof(*packet);
    }

    return true;
}

static bool
parse_header(pool_t pool, struct translate_response *response,
             const char *payload, size_t payload_length)
{
    const char *value = memchr(payload, ':', payload_length);
    if (value == NULL || value == payload) {
        daemon_log(2, "malformed HEADER packet\n");
        return false;
    }

    char *name = p_strndup(pool, payload, value - payload);
    ++value;

    str_to_lower(name);

    if (!http_header_name_valid(name)) {
        daemon_log(2, "malformed name in HEADER packet\n");
        return false;
    } else if (http_header_is_hop_by_hop(name)) {
        daemon_log(2, "ignoring hop-by-hop HEADER packet\n");
        return true;
    }

    if (response->headers == NULL)
        response->headers = strmap_new(pool, 17);
    strmap_add(response->headers, name, value);

    return true;
}

/**
 * Returns false if the client has been closed.
 */
static bool
translate_handle_packet(struct translate_client *client,
                        unsigned command, const char *payload,
                        size_t payload_length)
{
    struct transformation *transformation;

    if (command == TRANSLATE_BEGIN) {
        if (client->response.status != (http_status_t)-1) {
            daemon_log(1, "double BEGIN from translation server\n");
            translate_client_abort(client);
            return false;
        }
    } else {
        if (client->response.status == (http_status_t)-1) {
            daemon_log(1, "no BEGIN from translation server\n");
            translate_client_abort(client);
            return false;
        }
    }

    switch ((enum beng_translation_command)command) {
    case TRANSLATE_END:
        stopwatch_event(client->stopwatch, "end");
        client->callback(&client->response, client->callback_ctx);
        translate_client_release(client, true);
        return false;

    case TRANSLATE_BEGIN:
        memset(&client->response, 0, sizeof(client->response));
        client->previous_command = command;
        client->resource_address = &client->response.address;

        client->response.request_header_forward =
            (struct header_forward_settings){
            .modes = {
                [HEADER_GROUP_IDENTITY] = HEADER_FORWARD_MANGLE,
                [HEADER_GROUP_CAPABILITIES] = HEADER_FORWARD_YES,
                [HEADER_GROUP_COOKIE] = HEADER_FORWARD_MANGLE,
                [HEADER_GROUP_OTHER] = HEADER_FORWARD_NO,
            },
        };

        client->response.response_header_forward =
            (struct header_forward_settings){
            .modes = {
                [HEADER_GROUP_IDENTITY] = HEADER_FORWARD_NO,
                [HEADER_GROUP_CAPABILITIES] = HEADER_FORWARD_YES,
                [HEADER_GROUP_COOKIE] = HEADER_FORWARD_MANGLE,
                [HEADER_GROUP_OTHER] = HEADER_FORWARD_NO,
            },
        };

        client->response.max_age = -1;
        client->response.user_max_age = -1;
        client->response.views = p_calloc(client->pool, sizeof(*client->response.views));
        client->transformation_view_tail = &client->response.views->next;
        client->transformation = NULL;
        client->transformation_tail = &client->response.views->transformation;
        break;

    case TRANSLATE_PARAM:
    case TRANSLATE_REMOTE_HOST:
    case TRANSLATE_WIDGET_TYPE:
    case TRANSLATE_USER_AGENT:
    case TRANSLATE_ARGS:
    case TRANSLATE_QUERY_STRING:
    case TRANSLATE_LOCAL_ADDRESS:
    case TRANSLATE_LOCAL_ADDRESS_STRING:
    case TRANSLATE_AUTHORIZATION:
        daemon_log(2, "misplaced translate request packet\n");
        break;

    case TRANSLATE_STATUS:
        if (payload_length != 2) {
            daemon_log(1, "size mismatch in STATUS packet from translation server\n");
            translate_client_abort(client);
            return false;
        }

        client->response.status = *(const uint16_t*)payload;

        if (!http_status_is_valid(client->response.status)) {
            daemon_log(1, "invalid HTTP status code %u\n",
                       client->response.status);
            translate_client_abort(client);
            return false;
        }

        break;

    case TRANSLATE_PATH:
        if (client->resource_address == NULL ||
            client->resource_address->type != RESOURCE_ADDRESS_NONE) {
            daemon_log(2, "misplaced TRANSLATE_PATH packet\n");
            break;
        }

        if (payload == NULL) {
            daemon_log(2, "malformed TRANSLATE_PATH packet\n");
            translate_client_abort(client);
            return false;
        }

        client->resource_address->type = RESOURCE_ADDRESS_LOCAL;
        memset(&client->resource_address->u.local, 0,
               sizeof(client->resource_address->u.local));
        client->resource_address->u.local.path = payload;
        break;

    case TRANSLATE_PATH_INFO:
        if (client->resource_address == NULL ||
            (client->resource_address->type != RESOURCE_ADDRESS_CGI &&
             client->resource_address->type != RESOURCE_ADDRESS_FASTCGI)) {
            /* don't emit this warning when the resource is a local
               path.  This combination might once be useful, but isn't
               currently used. */
            if (client->resource_address == NULL ||
                client->resource_address->type != RESOURCE_ADDRESS_LOCAL)
                daemon_log(2, "misplaced TRANSLATE_PATH_INFO packet\n");
            break;
        }

        client->resource_address->u.cgi.path_info = payload;
        break;

    case TRANSLATE_DEFLATED:
        if (client->resource_address == NULL ||
            client->resource_address->type != RESOURCE_ADDRESS_LOCAL) {
            daemon_log(2, "misplaced TRANSLATE_DEFLATED packet\n");
            break;
        }

        client->resource_address->u.local.deflated = payload;
        break;

    case TRANSLATE_GZIPPED:
        if (client->resource_address == NULL ||
            client->resource_address->type != RESOURCE_ADDRESS_LOCAL) {
            daemon_log(2, "misplaced TRANSLATE_GZIPPED packet\n");
            break;
        }

        client->resource_address->u.local.gzipped = payload;
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
            translate_client_abort(client);
            return false;
        }

        client->resource_address->type = RESOURCE_ADDRESS_HTTP;
        client->resource_address->u.http =
            uri_address_new(client->pool, payload);
        break;

    case TRANSLATE_REDIRECT:
        client->response.redirect = payload;
        break;

    case TRANSLATE_BOUNCE:
        client->response.bounce = payload;
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
        break;

    case TRANSLATE_DOMAIN:
        daemon_log(2, "deprecated TRANSLATE_DOMAIN packet\n");
        break;

    case TRANSLATE_CONTAINER:
        if (client->transformation == NULL ||
            client->transformation->type != TRANSFORMATION_PROCESS) {
            daemon_log(2, "misplaced TRANSLATE_CONTAINER packet\n");
            break;
        }

        client->transformation->u.processor.options |= PROCESSOR_CONTAINER;
        break;

    case TRANSLATE_UNTRUSTED:
        if (*payload == 0 || *payload == '.' || payload[strlen(payload) - 1] == '.') {
            daemon_log(2, "malformed TRANSLATE_UNTRUSTED packet\n");
            break;
        }

        client->response.untrusted = payload;
        break;

    case TRANSLATE_UNTRUSTED_PREFIX:
        break;

    case TRANSLATE_SCHEME:
        if (strncmp(payload, "http", 4) != 0) {
            daemon_log(2, "malformed TRANSLATE_SCHEME packet\n");
            translate_client_abort(client);
            return false;
        }

        client->response.scheme = payload;
        break;

    case TRANSLATE_HOST:
        client->response.host = payload;
        break;

    case TRANSLATE_URI:
        if (payload == NULL || *payload != '/') {
            daemon_log(2, "malformed TRANSLATE_URI packet\n");
            translate_client_abort(client);
            return false;
        }

        client->response.uri = payload;
        break;

    case TRANSLATE_STATEFUL:
        client->response.stateful = true;
        break;

    case TRANSLATE_SESSION:
        client->response.session = payload;
        break;

    case TRANSLATE_USER:
        client->response.user = payload;
        client->previous_command = command;
        break;

    case TRANSLATE_LANGUAGE:
        client->response.language = payload;
        break;

    case TRANSLATE_PIPE:
        if (client->resource_address == NULL ||
            client->resource_address->type != RESOURCE_ADDRESS_NONE) {
            daemon_log(2, "misplaced TRANSLATE_PIPE packet\n");
            break;
        }

        if (payload == NULL) {
            daemon_log(2, "malformed TRANSLATE_PIPE packet\n");
            translate_client_abort(client);
            return false;
        }

        client->resource_address->type = RESOURCE_ADDRESS_PIPE;
        memset(&client->resource_address->u.cgi, 0, sizeof(client->resource_address->u.cgi));
        client->resource_address->u.cgi.path = payload;
        break;

    case TRANSLATE_CGI:
        if (client->resource_address == NULL ||
            client->resource_address->type != RESOURCE_ADDRESS_NONE) {
            daemon_log(2, "misplaced TRANSLATE_CGI packet\n");
            break;
        }

        if (payload == NULL) {
            daemon_log(2, "malformed TRANSLATE_CGI packet\n");
            translate_client_abort(client);
            return false;
        }

        client->resource_address->type = RESOURCE_ADDRESS_CGI;
        memset(&client->resource_address->u.cgi, 0, sizeof(client->resource_address->u.cgi));
        client->resource_address->u.cgi.path = payload;
        client->resource_address->u.cgi.document_root = client->response.document_root;
        break;

    case TRANSLATE_FASTCGI:
        if (client->resource_address == NULL ||
            client->resource_address->type != RESOURCE_ADDRESS_NONE) {
            daemon_log(2, "misplaced TRANSLATE_FASTCGI packet\n");
            break;
        }

        if (payload == NULL) {
            daemon_log(2, "malformed TRANSLATE_FASTCGI packet\n");
            translate_client_abort(client);
            return false;
        }

        client->resource_address->type = RESOURCE_ADDRESS_FASTCGI;
        memset(&client->resource_address->u.cgi, 0, sizeof(client->resource_address->u.cgi));
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
            translate_client_abort(client);
            return false;
        }

        client->resource_address->type = RESOURCE_ADDRESS_AJP;
        client->resource_address->u.http =
            uri_address_new(client->pool, payload);
        break;

    case TRANSLATE_JAILCGI:
        if (client->resource_address != NULL &&
            (client->resource_address->type == RESOURCE_ADDRESS_CGI ||
             client->resource_address->type == RESOURCE_ADDRESS_FASTCGI))
            client->resource_address->u.cgi.jail = true;
        else if (client->resource_address != NULL &&
                 client->resource_address->type == RESOURCE_ADDRESS_LOCAL &&
                 client->resource_address->u.local.delegate != NULL &&
                 client->resource_address->u.local.document_root != NULL)
            client->resource_address->u.local.jail = true;
        else {
            daemon_log(2, "misplaced TRANSLATE_JAILCGI packet\n");
            break;
        }

        break;

    case TRANSLATE_INTERPRETER:
        if (client->resource_address == NULL ||
            (client->resource_address->type != RESOURCE_ADDRESS_CGI &&
             client->resource_address->type != RESOURCE_ADDRESS_FASTCGI) ||
            client->resource_address->u.cgi.interpreter != NULL) {
            daemon_log(2, "misplaced TRANSLATE_INTERPRETER packet\n");
            break;
        }

        client->resource_address->u.cgi.interpreter = payload;
        break;

    case TRANSLATE_ACTION:
        if (client->resource_address == NULL ||
            (client->resource_address->type != RESOURCE_ADDRESS_CGI &&
             client->resource_address->type != RESOURCE_ADDRESS_FASTCGI) ||
            client->resource_address->u.cgi.action != NULL) {
            daemon_log(2, "misplaced TRANSLATE_ACTION packet\n");
            break;
        }

        client->resource_address->u.cgi.action = payload;
        break;

    case TRANSLATE_SCRIPT_NAME:
        if (client->resource_address == NULL ||
            (client->resource_address->type != RESOURCE_ADDRESS_CGI &&
             client->resource_address->type != RESOURCE_ADDRESS_FASTCGI) ||
            client->resource_address->u.cgi.script_name != NULL) {
            daemon_log(2, "misplaced TRANSLATE_SCRIPT_NAME packet\n");
            break;
        }

        client->resource_address->u.cgi.script_name = payload;
        break;

    case TRANSLATE_DOCUMENT_ROOT:
        if (client->resource_address != NULL &&
            (client->resource_address->type == RESOURCE_ADDRESS_CGI ||
             client->resource_address->type == RESOURCE_ADDRESS_FASTCGI))
            client->resource_address->u.cgi.document_root = payload;
        else if (client->resource_address != NULL &&
                 client->resource_address->type == RESOURCE_ADDRESS_LOCAL &&
                 client->resource_address->u.local.delegate != NULL)
            client->resource_address->u.local.document_root = payload;
        else
            client->response.document_root = payload;
        break;

    case TRANSLATE_ADDRESS:
        if (client->resource_address->type != RESOURCE_ADDRESS_HTTP &&
            client->resource_address->type != RESOURCE_ADDRESS_AJP) {
            daemon_log(2, "misplaced TRANSLATE_ADDRESS packet\n");
            break;
        }

        if (payload_length < 2) {
            daemon_log(2, "malformed TRANSLATE_ADDRESS packet\n");
            translate_client_abort(client);
            return false;
        }

        uri_address_add(client->resource_address->u.http,
                        (const struct sockaddr *)payload, payload_length);
        break;


    case TRANSLATE_ADDRESS_STRING:
        if (client->resource_address->type != RESOURCE_ADDRESS_HTTP &&
            client->resource_address->type != RESOURCE_ADDRESS_AJP) {
            daemon_log(2, "misplaced TRANSLATE_ADDRESS_STRING packet\n");
            break;
        }

        if (payload_length < 7) {
            daemon_log(2, "malformed TRANSLATE_ADDRESS_STRING packet\n");
            translate_client_abort(client);
            return false;
        }

        {
            bool ret;

            ret = parse_address_string(client->resource_address->u.http, payload);
            if (!ret) {
                daemon_log(2, "malformed TRANSLATE_ADDRESS_STRING packet\n");
                translate_client_abort(client);
                return false;
            }
        }

        break;

    case TRANSLATE_VIEW:
        add_view(client, payload);
        break;

    case TRANSLATE_MAX_AGE:
        if (payload_length != 4) {
            daemon_log(2, "malformed TRANSLATE_MAX_AGE packet\n");
            translate_client_abort(client);
            return false;
        }

        switch (client->previous_command) {
        case TRANSLATE_BEGIN:
            client->response.max_age = *(const uint32_t *)payload;
            break;

        case TRANSLATE_USER:
            client->response.user_max_age = *(const uint32_t *)payload;
            break;

        default:
            daemon_log(2, "misplaced TRANSLATE_MAX_AGE packet\n");
            break;
        }

        break;

    case TRANSLATE_VARY:
        if (payload_length == 0 ||
            payload_length % sizeof(client->response.vary[0]) != 0) {
            daemon_log(2, "malformed TRANSLATE_VARY packet\n");
            translate_client_abort(client);
            return false;
        }

        client->response.vary = (const uint16_t *)payload;
        client->response.num_vary = payload_length / sizeof(client->response.vary[0]);
        break;

    case TRANSLATE_INVALIDATE:
        if (payload_length == 0 ||
            payload_length % sizeof(client->response.invalidate[0]) != 0) {
            daemon_log(2, "malformed TRANSLATE_INVALIDATE packet\n");
            translate_client_abort(client);
            return false;
        }

        client->response.invalidate = (const uint16_t *)payload;
        client->response.num_invalidate = payload_length /
            sizeof(client->response.invalidate[0]);
        break;

    case TRANSLATE_BASE:
        client->response.base = payload;
        break;

    case TRANSLATE_DELEGATE:
        if (client->resource_address == NULL ||
            client->resource_address->type != RESOURCE_ADDRESS_LOCAL) {
            daemon_log(2, "misplaced TRANSLATE_DELEGATE packet\n");
            break;
        }

        if (payload == NULL) {
            daemon_log(2, "malformed TRANSLATE_DELEGATE packet\n");
            translate_client_abort(client);
            return false;
        }

        client->resource_address->u.local.delegate = payload;
        break;

    case TRANSLATE_APPEND:
        if (client->resource_address == NULL ||
            client->resource_address->type != RESOURCE_ADDRESS_PIPE) {
            daemon_log(2, "misplaced TRANSLATE_APPEND packet\n");
            break;
        }

        if (client->resource_address->u.cgi.num_args >=
            G_N_ELEMENTS(client->resource_address->u.cgi.args)) {
            daemon_log(2, "too many TRANSLATE_APPEND packets\n");
            break;
        }

        if (payload == NULL) {
            daemon_log(2, "malformed TRANSLATE_PIPE packet\n");
            translate_client_abort(client);
            return false;
        }

        client->resource_address->u.cgi.args[client->resource_address->u.cgi.num_args++] = payload;
        break;

    case TRANSLATE_PAIR:
        if (client->resource_address != NULL &&
            client->resource_address->type == RESOURCE_ADDRESS_FASTCGI) {
            if (client->resource_address->u.cgi.num_args >=
                G_N_ELEMENTS(client->resource_address->u.cgi.args)) {
                daemon_log(2, "too many TRANSLATE_PAIR packets\n");
                translate_client_abort(client);
                return false;
            }

            if (payload == NULL || *payload == '=' ||
                strchr(payload + 1, '=') == NULL) {
                daemon_log(2, "malformed TRANSLATE_PAIR packet\n");
                translate_client_abort(client);
                return false;
            }

            client->resource_address->u.cgi.args[client->resource_address->u.cgi.num_args++] = payload;
        } else
            daemon_log(2, "misplaced TRANSLATE_PAIR packet\n");

        break;

    case TRANSLATE_DISCARD_SESSION:
        client->response.discard_session = true;
        break;

    case TRANSLATE_REQUEST_HEADER_FORWARD:
        parse_header_forward(&client->response.request_header_forward,
                             payload, payload_length);
        break;

    case TRANSLATE_RESPONSE_HEADER_FORWARD:
        parse_header_forward(&client->response.response_header_forward,
                             payload, payload_length);
        break;

    case TRANSLATE_WWW_AUTHENTICATE:
        client->response.www_authenticate = payload;
        break;

    case TRANSLATE_AUTHENTICATION_INFO:
        client->response.authentication_info = payload;
        break;

    case TRANSLATE_HEADER:
        if (!parse_header(client->pool, &client->response,
                          payload, payload_length)) {
            translate_client_abort(client);
            return false;
        }

        break;
    }

    return true;
}

static void
translate_try_read(struct translate_client *client, int fd)
{
    bool bret;

    while (true) {
        switch (packet_reader_read(client->pool, &client->reader, fd)) {
        case PACKET_READER_INCOMPLETE: {
            struct timeval tv = {
                .tv_sec = 60,
                .tv_usec = 0,
            };

            p_event_add(&client->event, &tv, client->pool, "translate_event");
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

        bret = translate_handle_packet(client,
                                       client->reader.header.command,
                                       client->reader.payload == NULL
                                       ? "" : client->reader.payload,
                                       client->reader.header.length);
        if (!bret)
            break;

        packet_reader_init(&client->reader);
    }
}

static void
translate_read_event_callback(int fd, short event, void *ctx)
{
    struct translate_client *client = ctx;

    p_event_consumed(&client->event, client->pool);

    if (event == EV_TIMEOUT) {
        daemon_log(1, "read timeout on translation server\n");
        translate_client_abort(client);
        return;
    }

    translate_try_read(client, fd);
}


/*
 * send requests
 *
 */

static void
translate_try_write(struct translate_client *client, int fd)
{
    ssize_t nbytes;
    struct timeval tv = {
        .tv_sec = 10,
        .tv_usec = 0,
    };

    nbytes = send_from_gb(fd, client->request);
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

        stopwatch_event(client->stopwatch, "request");

        packet_reader_init(&client->reader);

        p_event_del(&client->event, client->pool);
        event_set(&client->event, fd, EV_READ|EV_TIMEOUT,
                  translate_read_event_callback, client);
        translate_try_read(client, fd);
        return;
    }

    p_event_add(&client->event, &tv, client->pool, "translate_event");
}

static void
translate_write_event_callback(int fd, short event, void *ctx)
{
    struct translate_client *client = ctx;

    p_event_consumed(&client->event, client->pool);

    if (event == EV_TIMEOUT) {
        daemon_log(1, "write timeout on translation server\n");
        translate_client_abort(client);
        return;
    }

    translate_try_write(client, fd);
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

    stopwatch_event(client->stopwatch, "abort");
    translate_client_release(client, false);
}

static const struct async_operation_class translate_operation = {
    .abort = translate_connection_abort,
};


/*
 * constructor
 *
 */

void
translate(pool_t pool, int fd,
          const struct lease *lease, void *lease_ctx,
          const struct translate_request *request,
          translate_callback_t callback,
          void *ctx,
          struct async_operation_ref *async_ref)
{
    struct growing_buffer *gb;
    struct translate_client *client;

    assert(pool != NULL);
    assert(fd >= 0);
    assert(lease != NULL);
    assert(request != NULL);
    assert(request->uri != NULL || request->widget_type != NULL);
    assert(callback != NULL);

    gb = marshal_request(pool, request);
    if (gb == NULL) {
        struct lease_ref lease_ref;
        lease_ref_set(&lease_ref, lease, lease_ctx);
        lease_release(&lease_ref, true);

        callback(&error, ctx);
        pool_unref(pool);
        return;
    }

    client = p_malloc(pool, sizeof(*client));
    client->pool = pool;
    client->stopwatch = stopwatch_fd_new(pool, fd,
                                         request->uri != NULL ? request->uri
                                         : request->widget_type);
    client->fd = fd;
    lease_ref_set(&client->lease_ref, lease, lease_ctx);

    event_set(&client->event, fd, EV_WRITE|EV_TIMEOUT,
              translate_write_event_callback, client);

    client->request = gb;
    client->callback = callback;
    client->callback_ctx = ctx;
    client->response.status = (http_status_t)-1;

    async_init(&client->async, &translate_operation);
    async_ref_set(async_ref, &client->async);

    pool_ref(client->pool);
    translate_try_write(client, fd);
}
