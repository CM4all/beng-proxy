/*
 * Call the translation server.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "translate.h"
#include "transformation.h"
#include "widget-class.h"
#include "please.h"
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
#include <http/header.h>

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

    const struct translate_handler *handler;
    void *handler_ctx;

    struct packet_reader reader;
    struct translate_response response;

    enum beng_translation_command previous_command;

    /** the current resource address being edited */
    struct resource_address *resource_address;

    /** the current widget view */
    struct widget_view *view;

    /** pointer to the tail of the transformation view linked list */
    struct widget_view **widget_view_tail;

    /** the current transformation */
    struct transformation *transformation;

    /** pointer to the tail of the transformation linked list */
    struct transformation **transformation_tail;

    /** this asynchronous operation is the translate request; aborting
        it causes the request to be cancelled */
    struct async_operation async;
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
    p_lease_release(&client->lease_ref, reuse, client->pool);
    pool_unref(client->pool);
}

static void
translate_client_abort(struct translate_client *client, GError *error)
{
    stopwatch_event(client->stopwatch, "error");

    async_operation_finished(&client->async);
    client->handler->error(error, client->handler_ctx);
    translate_client_release(client, false);
}

static void
translate_client_error(struct translate_client *client, const char *msg)
{
    GError *error = g_error_new_literal(translate_quark(), 0, msg);
    translate_client_abort(client, error);
}


/*
 * request marshalling
 *
 */

static bool
write_packet_n(struct growing_buffer *gb, uint16_t command,
               const void *payload, size_t length, GError **error_r)
{
    static struct beng_translation_header header;

    if (length >= 0xffff) {
        g_set_error(error_r, translate_quark(), 0,
                    "payload for translate command %u too large",
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
             const char *payload, GError **error_r)
{
    return write_packet_n(gb, command, payload,
                          payload != NULL ? strlen(payload) : 0,
                          error_r);
}

static bool
write_strref(struct growing_buffer *gb, uint16_t command,
             const struct strref *payload, GError **error_r)
{
    return write_packet_n(gb, command, payload->data, payload->length,
                          error_r);
}

/**
 * Forward the command to write_packet() only if #payload is not NULL.
 */
static bool
write_optional_packet(struct growing_buffer *gb, uint16_t command,
                      const char *payload, GError **error_r)
{
    if (payload == NULL)
        return true;

    return write_packet(gb, command, payload, error_r);
}

/**
 * Forward the command to write_packet() only if #payload is not NULL,
 * and strref_is_null(#payload) is false.
 */
static bool
write_optional_strref(struct growing_buffer *gb, uint16_t command,
                      const struct strref *payload, GError **error_r)
{
    return payload == NULL || strref_is_null(payload) ||
        write_strref(gb, command, payload, error_r);
}

static bool
write_short(struct growing_buffer *gb,
            uint16_t command, uint16_t payload, GError **error_r)
{
    return write_packet_n(gb, command, &payload, sizeof(payload), error_r);
}

static bool
write_sockaddr(struct growing_buffer *gb,
               uint16_t command, uint16_t command_string,
               const struct sockaddr *address, size_t address_length,
               GError **error_r)
{
    char address_string[1024];

    assert(address != NULL);
    assert(address_length > 0);

    return write_packet_n(gb, command, address, address_length, error_r) &&
        (!socket_address_to_string(address_string, sizeof(address_string),
                                   address, address_length) ||
         write_packet(gb, command_string, address_string, error_r));
}

static bool
write_optional_sockaddr(struct growing_buffer *gb,
                        uint16_t command, uint16_t command_string,
                        const struct sockaddr *address, size_t address_length,
                        GError **error_r)
{
    assert((address == NULL) == (address_length == 0));

    return address != NULL
        ? write_sockaddr(gb, command, command_string, address, address_length,
                         error_r)
        : true;
}

static struct growing_buffer *
marshal_request(pool_t pool, const struct translate_request *request,
                GError **error_r)
{
    struct growing_buffer *gb;
    bool success;

    gb = growing_buffer_new(pool, 512);

    success = write_packet(gb, TRANSLATE_BEGIN, NULL, error_r) &&
        (request->error_document_status == 0 ||
         (write_packet(gb, TRANSLATE_ERROR_DOCUMENT, "", error_r) &&
          write_short(gb, TRANSLATE_STATUS,
                      request->error_document_status, error_r))) &&
        write_optional_sockaddr(gb, TRANSLATE_LOCAL_ADDRESS,
                                TRANSLATE_LOCAL_ADDRESS_STRING,
                                request->local_address,
                                request->local_address_length, error_r) &&
        write_optional_packet(gb, TRANSLATE_REMOTE_HOST,
                              request->remote_host, error_r) &&
        write_optional_packet(gb, TRANSLATE_HOST, request->host, error_r) &&
        write_optional_packet(gb, TRANSLATE_USER_AGENT, request->user_agent,
                              error_r) &&
        write_optional_packet(gb, TRANSLATE_LANGUAGE,
                              request->accept_language, error_r) &&
        write_optional_packet(gb, TRANSLATE_AUTHORIZATION,
                              request->authorization, error_r) &&
        write_optional_packet(gb, TRANSLATE_URI, request->uri, error_r) &&
        write_optional_packet(gb, TRANSLATE_ARGS, request->args, error_r) &&
        write_optional_packet(gb, TRANSLATE_QUERY_STRING,
                              request->query_string, error_r) &&
        write_optional_packet(gb, TRANSLATE_WIDGET_TYPE,
                              request->widget_type, error_r) &&
        write_optional_packet(gb, TRANSLATE_SESSION, request->session,
                              error_r) &&
        write_optional_strref(gb, TRANSLATE_CHECK, &request->check,
                              error_r) &&
        write_optional_packet(gb, TRANSLATE_PARAM, request->param,
                              error_r) &&
        write_packet(gb, TRANSLATE_END, NULL, error_r);
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

/**
 * Finish the settings in the current view, i.e. copy attributes from
 * the "parent" view.
 */
static void
finish_view(struct translate_client *client)
{
    assert(client != NULL);
    assert(client->response.views != NULL);

    struct widget_view *view = client->view;
    if (client->view == NULL) {
        view = client->response.views;
        assert(view != NULL);

        const struct resource_address *address = &client->response.address;
        if (address->type != RESOURCE_ADDRESS_NONE &&
            view->address.type == RESOURCE_ADDRESS_NONE) {
            /* no address yet: copy address from response */
            view->address = *address;
            view->filter_4xx = client->response.filter_4xx;
        }
    } else {
        if (client->view->address.type == RESOURCE_ADDRESS_NONE &&
            client->view != client->response.views)
            /* no address yet: inherits settings from the default view */
            widget_view_inherit_from(client->pool, client->view,
                                     client->response.views);
    }
}

static void
add_view(struct translate_client *client, const char *name)
{
    finish_view(client);

    struct widget_view *view;

    view = p_malloc(client->pool, sizeof(*view));
    widget_view_init(view);
    view->name = name;

    client->view = view;
    *client->widget_view_tail = view;
    client->widget_view_tail = &view->next;
    client->resource_address = &view->address;
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
             const char *payload, size_t payload_length,
             GError **error_r)
{
    const char *value = memchr(payload, ':', payload_length);
    if (value == NULL || value == payload) {
        g_set_error(error_r, translate_quark(), 0, "malformed HEADER packet");
        return false;
    }

    char *name = p_strndup(pool, payload, value - payload);
    ++value;

    str_to_lower(name);

    if (!http_header_name_valid(name)) {
        g_set_error(error_r, translate_quark(), 0,
                    "malformed name in HEADER packet");
        return false;
    } else if (http_header_is_hop_by_hop(name)) {
        g_set_error(error_r, translate_quark(), 0, "hop-by-hop HEADER packet");
        return false;
    }

    if (response->headers == NULL)
        response->headers = strmap_new(pool, 17);
    strmap_add(response->headers, name, value);

    return true;
}

static bool
translate_jail_finish(struct jail_params *jail,
                      const struct translate_response *response,
                      const char *document_root,
                      GError **error_r)
{
    if (!jail->enabled)
        return true;

    if (jail->home_directory == NULL)
        jail->home_directory = document_root;

    if (jail->home_directory == NULL) {
        g_set_error(error_r, translate_quark(), 0,
                    "No home directory for JAIL");
        return false;
    }

    if (jail->site_id == NULL)
        jail->site_id = response->site;

    return true;
}

/**
 * Final fixups for the response before it is passed to the handler.
 */
static bool
translate_response_finish(struct translate_response *response,
                          GError **error_r)
{
    if (response->address.type == RESOURCE_ADDRESS_CGI ||
        response->address.type == RESOURCE_ADDRESS_WAS ||
        response->address.type == RESOURCE_ADDRESS_FASTCGI) {
        if (response->address.u.cgi.uri == NULL)
            response->address.u.cgi.uri = response->uri;

        if (response->address.u.cgi.document_root == NULL)
            response->address.u.cgi.document_root = response->document_root;

        if (!translate_jail_finish(&response->address.u.cgi.jail, response,
                                   response->address.u.cgi.document_root,
                                   error_r))
            return false;
    } else if (response->address.type == RESOURCE_ADDRESS_LOCAL) {
        if (response->address.u.local.jail.enabled &&
            response->address.u.local.document_root == NULL)
            response->address.u.local.document_root = response->document_root;

        if (!translate_jail_finish(&response->address.u.local.jail, response,
                                   response->address.u.local.document_root,
                                   error_r))
            return false;
    }

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
            GError *error =
                g_error_new_literal(translate_quark(), 0,
                                    "double BEGIN from translation server");
            translate_client_abort(client, error);
            return false;
        }
    } else {
        if (client->response.status == (http_status_t)-1) {
            GError *error =
                g_error_new_literal(translate_quark(), 0,
                                    "no BEGIN from translation server");
            translate_client_abort(client, error);
            return false;
        }
    }

    switch ((enum beng_translation_command)command) {
        GError *error = NULL;

    case TRANSLATE_END:
        stopwatch_event(client->stopwatch, "end");

        if (!translate_response_finish(&client->response, &error)) {
            translate_client_abort(client, error);
            return false;
        }

        finish_view(client);

        async_operation_finished(&client->async);
        client->handler->response(&client->response, client->handler_ctx);
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
        client->response.views = p_malloc(client->pool, sizeof(*client->response.views));
        widget_view_init(client->response.views);
        client->view = NULL;
        client->widget_view_tail = &client->response.views->next;
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
            translate_client_error(client,
                                   "size mismatch in STATUS packet from translation server");
            return false;
        }

        client->response.status = *(const uint16_t*)payload;

        if (!http_status_is_valid(client->response.status)) {
            error = g_error_new(translate_quark(), 0,
                                "invalid HTTP status code %u",
                                client->response.status);
            translate_client_abort(client, error);
            return false;
        }

        break;

    case TRANSLATE_PATH:
        if (client->resource_address == NULL ||
            client->resource_address->type != RESOURCE_ADDRESS_NONE) {
            translate_client_error(client, "misplaced TRANSLATE_PATH packet");
            return false;
        }

        if (payload == NULL) {
            translate_client_error(client, "malformed TRANSLATE_PATH packet");
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
             client->resource_address->type != RESOURCE_ADDRESS_WAS &&
             client->resource_address->type != RESOURCE_ADDRESS_FASTCGI)) {
            /* don't emit this error when the resource is a local
               path.  This combination might once be useful, but isn't
               currently used. */
            if (client->resource_address == NULL ||
                client->resource_address->type != RESOURCE_ADDRESS_LOCAL) {
                translate_client_error(client,
                                       "misplaced TRANSLATE_PATH_INFO packet");
                return false;
            }

            break;
        }

        client->resource_address->u.cgi.path_info = payload;
        break;

    case TRANSLATE_DEFLATED:
        if (client->resource_address == NULL ||
            client->resource_address->type != RESOURCE_ADDRESS_LOCAL) {
            translate_client_error(client,
                                   "misplaced TRANSLATE_DEFLATED packet");
            return false;
        }

        client->resource_address->u.local.deflated = payload;
        break;

    case TRANSLATE_GZIPPED:
        if (client->resource_address == NULL ||
            client->resource_address->type != RESOURCE_ADDRESS_LOCAL) {
            translate_client_error(client, "misplaced TRANSLATE_GZIPPED packet");
            return false;
        }

        client->resource_address->u.local.gzipped = payload;
        break;

    case TRANSLATE_SITE:
        if (client->resource_address == NULL)
            client->response.site = payload;
        else if (client->resource_address->type == RESOURCE_ADDRESS_CGI ||
                 client->resource_address->type == RESOURCE_ADDRESS_WAS ||
                 client->resource_address->type == RESOURCE_ADDRESS_FASTCGI)
            client->resource_address->u.cgi.jail.site_id = payload;
        else if (client->resource_address->type == RESOURCE_ADDRESS_LOCAL ||
                 client->resource_address->u.local.jail.enabled)
            client->resource_address->u.local.jail.site_id = payload;
        else {
            translate_client_error(client, "misplaced TRANSLATE_SITE packet");
            return false;
        }

        break;

    case TRANSLATE_CONTENT_TYPE:
        if (client->resource_address == NULL ||
            client->resource_address->type != RESOURCE_ADDRESS_LOCAL) {
            translate_client_error(client, "misplaced TRANSLATE_CONTENT_TYPE packet");
            return false;
        }

        client->resource_address->u.local.content_type = payload;
        break;

    case TRANSLATE_PROXY:
        if (client->resource_address == NULL ||
            client->resource_address->type != RESOURCE_ADDRESS_NONE) {
            translate_client_error(client, "misplaced TRANSLATE_PROXY packet");
            return false;
        }

        if (payload == NULL) {
            translate_client_error(client, "malformed TRANSLATE_PROXY packet");
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

    case TRANSLATE_FILTER_4XX:
        if (client->view != NULL)
            client->view->filter_4xx = true;
        else
            client->response.filter_4xx = true;
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
            translate_client_error(client,
                                   "misplaced TRANSLATE_CONTAINER packet");
            return false;
        }

        client->transformation->u.processor.options |= PROCESSOR_CONTAINER;
        break;

    case TRANSLATE_UNTRUSTED:
        if (*payload == 0 || *payload == '.' || payload[strlen(payload) - 1] == '.') {
            translate_client_error(client,
                                   "malformed TRANSLATE_UNTRUSTED packet");
            return false;
        }

        if (client->response.untrusted_prefix != NULL) {
            translate_client_error(client,
                                   "misplaced TRANSLATE_UNTRUSTED packet");
            return false;
        }

        client->response.untrusted = payload;
        break;

    case TRANSLATE_UNTRUSTED_PREFIX:
        if (*payload == 0 || *payload == '.' || payload[strlen(payload) - 1] == '.') {
            translate_client_error(client,
                                   "malformed TRANSLATE_UNTRUSTED_PREFIX packet");
            return false;
        }

        if (client->response.untrusted != NULL) {
            translate_client_error(client,
                                   "misplaced TRANSLATE_UNTRUSTED_PREFIX packet");
            return false;
        }

        client->response.untrusted_prefix = payload;
        break;

    case TRANSLATE_SCHEME:
        if (strncmp(payload, "http", 4) != 0) {
            translate_client_error(client,
                                   "misplaced TRANSLATE_SCHEME packet");
            return false;
        }

        client->response.scheme = payload;
        break;

    case TRANSLATE_HOST:
        client->response.host = payload;
        break;

    case TRANSLATE_URI:
        if (payload == NULL || *payload != '/') {
            translate_client_error(client, "malformed TRANSLATE_URI packet");
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
            translate_client_error(client, "misplaced TRANSLATE_PIPE packet");
            return false;
        }

        if (payload == NULL) {
            translate_client_error(client, "malformed TRANSLATE_PIPE packet");
            return false;
        }

        client->resource_address->type = RESOURCE_ADDRESS_PIPE;
        memset(&client->resource_address->u.cgi, 0, sizeof(client->resource_address->u.cgi));
        client->resource_address->u.cgi.path = payload;
        break;

    case TRANSLATE_CGI:
        if (client->resource_address == NULL ||
            client->resource_address->type != RESOURCE_ADDRESS_NONE) {
            translate_client_error(client, "misplaced TRANSLATE_CGI packet");
            return false;
        }

        if (payload == NULL) {
            translate_client_error(client, "malformed TRANSLATE_CGI packet");
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
            translate_client_error(client,
                                   "misplaced TRANSLATE_FASTCGI packet");
            break;
        }

        if (payload == NULL) {
            translate_client_error(client,
                                   "malformed TRANSLATE_FASTCGI packet");
            return false;
        }

        client->resource_address->type = RESOURCE_ADDRESS_FASTCGI;
        memset(&client->resource_address->u.cgi, 0, sizeof(client->resource_address->u.cgi));
        client->resource_address->u.cgi.path = payload;
        break;

    case TRANSLATE_AJP:
        if (client->resource_address == NULL ||
            client->resource_address->type != RESOURCE_ADDRESS_NONE) {
            translate_client_error(client, "misplaced TRANSLATE_AJP packet");
            return false;
        }

        if (payload == NULL) {
            translate_client_error(client, "malformed TRANSLATE_AJP packet");
            return false;
        }

        client->resource_address->type = RESOURCE_ADDRESS_AJP;
        client->resource_address->u.http =
            uri_address_new(client->pool, payload);
        break;

    case TRANSLATE_JAILCGI:
        if (client->resource_address != NULL &&
            (client->resource_address->type == RESOURCE_ADDRESS_CGI ||
             client->resource_address->type == RESOURCE_ADDRESS_WAS ||
             client->resource_address->type == RESOURCE_ADDRESS_FASTCGI))
            client->resource_address->u.cgi.jail.enabled = true;
        else if (client->resource_address != NULL &&
                 client->resource_address->type == RESOURCE_ADDRESS_LOCAL &&
                 client->resource_address->u.local.delegate != NULL)
            client->resource_address->u.local.jail.enabled = true;
        else {
            translate_client_error(client,
                                   "misplaced TRANSLATE_JAILCGI packet");
            return false;
        }

        break;

    case TRANSLATE_HOME:
        if ((client->resource_address->type == RESOURCE_ADDRESS_CGI ||
             client->resource_address->type == RESOURCE_ADDRESS_WAS ||
             client->resource_address->type == RESOURCE_ADDRESS_FASTCGI) &&
            client->resource_address->u.cgi.jail.enabled &&
            client->resource_address->u.cgi.jail.home_directory == NULL)
            client->resource_address->u.cgi.jail.home_directory = payload;
        else if (client->resource_address->type == RESOURCE_ADDRESS_LOCAL &&
                 client->resource_address->u.local.jail.enabled &&
                 client->resource_address->u.local.jail.home_directory == NULL)
            client->resource_address->u.local.jail.home_directory = payload;
        else {
            translate_client_error(client,
                                   "misplaced TRANSLATE_HOME packet");
            return false;
        }

        break;

    case TRANSLATE_INTERPRETER:
        if (client->resource_address == NULL ||
            (client->resource_address->type != RESOURCE_ADDRESS_CGI &&
             client->resource_address->type != RESOURCE_ADDRESS_FASTCGI) ||
            client->resource_address->u.cgi.interpreter != NULL) {
            translate_client_error(client,
                                   "misplaced TRANSLATE_INTERPRETER packet");
            return false;
        }

        client->resource_address->u.cgi.interpreter = payload;
        break;

    case TRANSLATE_ACTION:
        if (client->resource_address == NULL ||
            (client->resource_address->type != RESOURCE_ADDRESS_CGI &&
             client->resource_address->type != RESOURCE_ADDRESS_FASTCGI) ||
            client->resource_address->u.cgi.action != NULL) {
            translate_client_error(client,
                                   "misplaced TRANSLATE_ACTION packet");
            return false;
        }

        client->resource_address->u.cgi.action = payload;
        break;

    case TRANSLATE_SCRIPT_NAME:
        if (client->resource_address == NULL ||
            (client->resource_address->type != RESOURCE_ADDRESS_CGI &&
             client->resource_address->type != RESOURCE_ADDRESS_WAS &&
             client->resource_address->type != RESOURCE_ADDRESS_FASTCGI) ||
            client->resource_address->u.cgi.script_name != NULL) {
            translate_client_error(client,
                                   "misplaced TRANSLATE_SCRIPT_NAME packet");
            return false;
        }

        client->resource_address->u.cgi.script_name = payload;
        break;

    case TRANSLATE_DOCUMENT_ROOT:
        if (client->resource_address != NULL &&
            (client->resource_address->type == RESOURCE_ADDRESS_CGI ||
             client->resource_address->type == RESOURCE_ADDRESS_WAS ||
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
            translate_client_error(client,
                                   "misplaced TRANSLATE_ADDRESS packet");
            return false;
        }

        if (payload_length < 2) {
            translate_client_error(client,
                                   "malformed TRANSLATE_INTERPRETER packet");
            return false;
        }

        uri_address_add(client->resource_address->u.http,
                        (const struct sockaddr *)payload, payload_length);
        break;


    case TRANSLATE_ADDRESS_STRING:
        if (client->resource_address->type != RESOURCE_ADDRESS_HTTP &&
            client->resource_address->type != RESOURCE_ADDRESS_AJP) {
            translate_client_error(client,
                                   "misplaced TRANSLATE_ADDRESS_STRING packet");
            return false;
        }

        if (payload_length < 7) {
            translate_client_error(client,
                                   "malformed TRANSLATE_ADDRESS_STRING packet");
            return false;
        }

        {
            bool ret;

            ret = parse_address_string(client->resource_address->u.http, payload);
            if (!ret) {
                translate_client_error(client,
                                       "malformed TRANSLATE_ADDRESS_STRING packet");
                return false;
            }
        }

        break;

    case TRANSLATE_VIEW:
        if (!valid_view_name(payload)) {
            translate_client_error(client, "invalid view name");
            return false;
        }

        add_view(client, payload);
        break;

    case TRANSLATE_MAX_AGE:
        if (payload_length != 4) {
            translate_client_error(client,
                                   "malformed TRANSLATE_MAX_AGE packet");
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
            translate_client_error(client,
                                   "misplaced TRANSLATE_MAX_AGE packet");
            return false;
        }

        break;

    case TRANSLATE_VARY:
        if (payload_length == 0 ||
            payload_length % sizeof(client->response.vary[0]) != 0) {
            translate_client_error(client, "malformed TRANSLATE_VARY packet");
            return false;
        }

        client->response.vary = (const uint16_t *)payload;
        client->response.num_vary = payload_length / sizeof(client->response.vary[0]);
        break;

    case TRANSLATE_INVALIDATE:
        if (payload_length == 0 ||
            payload_length % sizeof(client->response.invalidate[0]) != 0) {
            translate_client_error(client,
                                   "malformed TRANSLATE_INVALIDATE packet");
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
            translate_client_error(client,
                                   "misplaced TRANSLATE_DELEGATE packet");
            return false;
        }

        if (payload == NULL) {
            translate_client_error(client,
                                   "malformed TRANSLATE_DELEGATE packet");
            return false;
        }

        client->resource_address->u.local.delegate = payload;
        break;

    case TRANSLATE_APPEND:
        if (client->resource_address == NULL ||
            client->resource_address->type != RESOURCE_ADDRESS_PIPE) {
            translate_client_error(client,
                                   "misplaced TRANSLATE_APPEND packet");
            return false;
        }

        if (client->resource_address->u.cgi.num_args >=
            G_N_ELEMENTS(client->resource_address->u.cgi.args)) {
            translate_client_error(client,
                                   "too many TRANSLATE_APPEND packets");
            return false;
        }

        if (payload == NULL) {
            translate_client_error(client,
                                   "malformed TRANSLATE_APPEND packet");
            return false;
        }

        client->resource_address->u.cgi.args[client->resource_address->u.cgi.num_args++] = payload;
        break;

    case TRANSLATE_PAIR:
        if (client->resource_address != NULL &&
            (client->resource_address->type == RESOURCE_ADDRESS_FASTCGI ||
             client->resource_address->type == RESOURCE_ADDRESS_WAS)) {
            if (client->resource_address->u.cgi.num_args >=
                G_N_ELEMENTS(client->resource_address->u.cgi.args)) {
                translate_client_error(client,
                                       "too many TRANSLATE_PAIR packets");
                return false;
            }

            if (payload == NULL || *payload == '=' ||
                strchr(payload + 1, '=') == NULL) {
                translate_client_error(client,
                                       "malformed TRANSLATE_PAIR packet");
                return false;
            }

            client->resource_address->u.cgi.args[client->resource_address->u.cgi.num_args++] = payload;
        } else {
            translate_client_error(client,
                                   "misplaced TRANSLATE_PAIR packet");
            return false;
        }

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
                          payload, payload_length, &error)) {
            translate_client_abort(client, error);
            return false;
        }

        break;

    case TRANSLATE_SECURE_COOKIE:
        client->response.secure_cookie = true;
        break;

    case TRANSLATE_ERROR_DOCUMENT:
        client->response.error_document = true;
        break;

    case TRANSLATE_CHECK:
        if (payload != NULL)
            strref_set(&client->response.check, payload, payload_length);
        else
            strref_set(&client->response.check, "", 0);
        break;

    case TRANSLATE_PREVIOUS:
        client->response.previous = true;
        break;

    case TRANSLATE_WAS:
        if (client->resource_address == NULL ||
            client->resource_address->type != RESOURCE_ADDRESS_NONE) {
            translate_client_error(client,
                                   "misplaced TRANSLATE_WAS packet");
            return false;
        }

        if (payload == NULL) {
            translate_client_error(client,
                                   "malformed TRANSLATE_WAS packet");
            return false;
        }

        client->resource_address->type = RESOURCE_ADDRESS_WAS;
        memset(&client->resource_address->u.cgi, 0, sizeof(client->resource_address->u.cgi));
        client->resource_address->u.cgi.path = payload;
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
            GError *error;

        case PACKET_READER_INCOMPLETE: {
            struct timeval tv = {
                .tv_sec = 60,
                .tv_usec = 0,
            };

            p_event_add(&client->event, &tv, client->pool, "translate_event");
            return;
        }

        case PACKET_READER_ERROR:
            error = g_error_new(g_file_error_quark(), errno,
                                "read error from translation server: %s",
                                strerror(errno));
            translate_client_abort(client, error);
            return;

        case PACKET_READER_EOF:
            translate_client_error(client, "translation server aborted the connection");
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
        translate_client_error(client, "translation read timeout");
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
        GError *error =
            g_error_new(g_file_error_quark(), errno,
                        "write error to translation server: %s",
                        strerror(errno));
        translate_client_abort(client, error);
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
        translate_client_error(client, "translation write timeout");
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
          const struct translate_handler *handler, void *ctx,
          struct async_operation_ref *async_ref)
{
    GError *error = NULL;
    struct growing_buffer *gb;
    struct translate_client *client;

    assert(pool != NULL);
    assert(fd >= 0);
    assert(lease != NULL);
    assert(request != NULL);
    assert(request->uri != NULL || request->widget_type != NULL);
    assert(handler != NULL);
    assert(handler->response != NULL);
    assert(handler->error != NULL);

    gb = marshal_request(pool, request, &error);
    if (gb == NULL) {
        lease_direct_release(lease, lease_ctx, true);

        handler->error(error, ctx);
        pool_unref(pool);
        return;
    }

    client = p_malloc(pool, sizeof(*client));
    client->pool = pool;
    client->stopwatch = stopwatch_fd_new(pool, fd,
                                         request->uri != NULL ? request->uri
                                         : request->widget_type);
    client->fd = fd;
    p_lease_ref_set(&client->lease_ref, lease, lease_ctx,
                    pool, "translate_lease");

    event_set(&client->event, fd, EV_WRITE|EV_TIMEOUT,
              translate_write_event_callback, client);

    client->request = gb;
    client->handler = handler;
    client->handler_ctx = ctx;
    client->response.status = (http_status_t)-1;

    async_init(&client->async, &translate_operation);
    async_ref_set(async_ref, &client->async);

    pool_ref(client->pool);
    translate_try_write(client, fd);
}
