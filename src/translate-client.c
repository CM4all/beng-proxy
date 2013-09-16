/*
 * Call the translation server.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "translate-client.h"
#include "translate-request.h"
#include "translate-response.h"
#include "buffered_socket.h"
#include "transformation.h"
#include "widget-class.h"
#include "please.h"
#include "growing-buffer.h"
#include "processor.h"
#include "css_processor.h"
#include "async.h"
#include "uri-address.h"
#include "lhttp_address.h"
#include "strutil.h"
#include "strmap.h"
#include "stopwatch.h"
#include "beng-proxy/translation.h"
#include "gerrno.h"

#include <daemon/log.h>
#include <socket/address.h>
#include <socket/resolver.h>
#include <http/header.h>

#include <glib.h>

#include <assert.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <sys/un.h>
#include <arpa/inet.h>
#include <stdlib.h>
#include <stdbool.h>
#include <netdb.h>

enum packet_reader_state {
    PACKET_READER_HEADER,
    PACKET_READER_PAYLOAD,
    PACKET_READER_COMPLETE,
};

struct packet_reader {
    enum packet_reader_state state;

    struct beng_translation_header header;

    char *payload;
    size_t payload_position;
};

struct translate_client {
    struct pool *pool;

    struct stopwatch *stopwatch;

    struct buffered_socket socket;
    struct lease_ref lease_ref;

    /** the marshalled translate request */
    struct growing_buffer_reader request;

    const struct translate_handler *handler;
    void *handler_ctx;

    struct packet_reader reader;
    struct translate_response response;

    enum beng_translation_command previous_command;

    /** the current resource address being edited */
    struct resource_address *resource_address;

    /** the current JailCGI parameters being edited */
    struct jail_params *jail;

    /** the current CGI/FastCGI/WAS address being edited */
    struct file_address *file_address;

    /** the current CGI/FastCGI/WAS address being edited */
    struct cgi_address *cgi_address;

    /** the current NFS address being edited */
    struct nfs_address *nfs_address;

    /** the current "local HTTP" address being edited */
    struct lhttp_address *lhttp_address;

    /** the current address list being edited */
    struct address_list *address_list;

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

static const struct timeval translate_read_timeout = {
    .tv_sec = 60,
    .tv_usec = 0,
};

static const struct timeval translate_write_timeout = {
    .tv_sec = 10,
    .tv_usec = 0,
};

static void
translate_client_release_socket(struct translate_client *client, bool reuse)
{
    assert(client != NULL);
    assert(buffered_socket_connected(&client->socket));

    stopwatch_dump(client->stopwatch);

    buffered_socket_abandon(&client->socket);
    buffered_socket_destroy(&client->socket);

    p_lease_release(&client->lease_ref, reuse, client->pool);
}

/**
 * Release resources held by this object: the event object, the socket
 * lease, and the pool reference.
 */
static void
translate_client_release(struct translate_client *client, bool reuse)
{
    assert(client != NULL);

    translate_client_release_socket(client, reuse);
    pool_unref(client->pool);
}

static void
translate_client_abort(struct translate_client *client, GError *error)
{
    stopwatch_event(client->stopwatch, "error");

    translate_client_release_socket(client, false);

    async_operation_finished(&client->async);
    client->handler->error(error, client->handler_ctx);
    pool_unref(client->pool);
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
marshal_request(struct pool *pool, const struct translate_request *request,
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
        write_optional_packet(gb, TRANSLATE_UA_CLASS, request->ua_class,
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
    reader->state = PACKET_READER_HEADER;
}

/**
 * Read a packet from the socket.
 *
 * @return the number of bytes consumed
 */
static size_t
packet_reader_feed(struct pool *pool, struct packet_reader *reader,
                   const uint8_t *data, size_t length)
{
    assert(reader->state == PACKET_READER_HEADER ||
           reader->state == PACKET_READER_PAYLOAD ||
           reader->state == PACKET_READER_COMPLETE);

    /* discard the packet that was completed (and consumed) by the
       previous call */
    if (reader->state == PACKET_READER_COMPLETE)
        reader->state = PACKET_READER_HEADER;

    size_t consumed = 0;

    if (reader->state == PACKET_READER_HEADER) {
        if (length < sizeof(reader->header))
            /* need more data */
            return 0;

        memcpy(&reader->header, data, sizeof(reader->header));

        if (reader->header.length == 0) {
            reader->payload = NULL;
            reader->state = PACKET_READER_COMPLETE;
            return sizeof(reader->header);
        }

        consumed += sizeof(reader->header);
        data += sizeof(reader->header);
        length -= sizeof(reader->header);

        reader->state = PACKET_READER_PAYLOAD;

        reader->payload_position = 0;
        reader->payload = p_malloc(pool, reader->header.length + 1);
        reader->payload[reader->header.length] = 0;

        if (length == 0)
            return consumed;
    }

    assert(reader->state == PACKET_READER_PAYLOAD);

    assert(reader->payload_position < reader->header.length);

    size_t nbytes = reader->header.length - reader->payload_position;
    if (nbytes > length)
        nbytes = length;

    memcpy(reader->payload + reader->payload_position, data, nbytes);
    reader->payload_position += nbytes;
    if (reader->payload_position == reader->header.length)
        reader->state = PACKET_READER_COMPLETE;

    consumed += nbytes;
    return consumed;
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
parse_address_string(struct pool *pool, struct address_list *list, const char *p)
{
    if (*p == '/') {
        /* unix domain socket */

        struct sockaddr_un sun;
        size_t path_length = strlen(p);

        if (path_length >= sizeof(sun.sun_path))
            return false;

        sun.sun_family = AF_UNIX;
        memcpy(sun.sun_path, p, path_length + 1);

        address_list_add(pool, list,
                         (const struct sockaddr *)&sun, SUN_LEN(&sun));
        return true;
    }

    struct addrinfo hints, *ai;
    int ret;

    memset(&hints, 0, sizeof(hints));
    hints.ai_flags = AI_NUMERICHOST;
    hints.ai_socktype = SOCK_STREAM;

    ret = socket_resolve_host_port(p, 80, &hints, &ai);
    if (ret != 0)
        return false;

    for (const struct addrinfo *i = ai; i != NULL; i = i->ai_next)
        address_list_add(pool, list, i->ai_addr, i->ai_addrlen);

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
            resource_address_copy(client->pool, &view->address, address);
            view->filter_4xx = client->response.filter_4xx;
        }

        view->request_header_forward = client->response.request_header_forward;
        view->response_header_forward = client->response.response_header_forward;
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
    view->request_header_forward = client->response.request_header_forward;
    view->response_header_forward = client->response.response_header_forward;

    client->view = view;
    *client->widget_view_tail = view;
    client->widget_view_tail = &view->next;
    client->resource_address = &view->address;
    client->jail = NULL;
    client->file_address = NULL;
    client->cgi_address = NULL;
    client->nfs_address = NULL;
    client->lhttp_address = NULL;
    client->address_list = NULL;
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
parse_header(struct pool *pool, struct translate_response *response,
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
    if (resource_address_is_cgi_alike(&response->address)) {
        struct cgi_address *cgi = resource_address_get_cgi(&response->address);

        if (cgi->uri == NULL)
            cgi->uri = response->uri;

        if (cgi->document_root == NULL)
            cgi->document_root = response->document_root;

        if (!translate_jail_finish(&cgi->jail, response, cgi->document_root,
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

    GError *error = NULL;

    switch ((enum beng_translation_command)command) {
        struct uri_with_address *uwa;

    case TRANSLATE_END:
        stopwatch_event(client->stopwatch, "end");

        if (!translate_response_finish(&client->response, &error)) {
            translate_client_abort(client, error);
            return false;
        }

        finish_view(client);

        translate_client_release_socket(client, true);

        async_operation_finished(&client->async);
        client->handler->response(&client->response, client->handler_ctx);
        pool_unref(client->pool);
        return false;

    case TRANSLATE_BEGIN:
        memset(&client->response, 0, sizeof(client->response));
        client->previous_command = command;
        client->resource_address = &client->response.address;
        client->jail = NULL;
        client->file_address = NULL;
        client->cgi_address = NULL;
        client->nfs_address = NULL;
        client->lhttp_address = NULL;
        client->address_list = NULL;

        client->response.request_header_forward =
            (struct header_forward_settings){
            .modes = {
                [HEADER_GROUP_IDENTITY] = HEADER_FORWARD_MANGLE,
                [HEADER_GROUP_CAPABILITIES] = HEADER_FORWARD_YES,
                [HEADER_GROUP_COOKIE] = HEADER_FORWARD_MANGLE,
                [HEADER_GROUP_OTHER] = HEADER_FORWARD_NO,
                [HEADER_GROUP_FORWARD] = HEADER_FORWARD_NO,
            },
        };

        client->response.response_header_forward =
            (struct header_forward_settings){
            .modes = {
                [HEADER_GROUP_IDENTITY] = HEADER_FORWARD_NO,
                [HEADER_GROUP_CAPABILITIES] = HEADER_FORWARD_YES,
                [HEADER_GROUP_COOKIE] = HEADER_FORWARD_MANGLE,
                [HEADER_GROUP_OTHER] = HEADER_FORWARD_NO,
                [HEADER_GROUP_FORWARD] = HEADER_FORWARD_NO,
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
        return true;

    case TRANSLATE_PARAM:
    case TRANSLATE_REMOTE_HOST:
    case TRANSLATE_WIDGET_TYPE:
    case TRANSLATE_USER_AGENT:
    case TRANSLATE_ARGS:
    case TRANSLATE_QUERY_STRING:
    case TRANSLATE_LOCAL_ADDRESS:
    case TRANSLATE_LOCAL_ADDRESS_STRING:
    case TRANSLATE_AUTHORIZATION:
    case TRANSLATE_UA_CLASS:
        daemon_log(2, "misplaced translate request packet\n");
        return true;

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

        return true;

    case TRANSLATE_PATH:
        if (client->nfs_address != NULL && *client->nfs_address->path == 0) {
            if (payload == NULL || *payload != '/') {
                translate_client_error(client,
                                       "malformed TRANSLATE_PATH packet");
                return false;
            }

            client->nfs_address->path = payload;
            return true;
        }

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
        client->file_address = &client->resource_address->u.local;
        file_address_init(client->file_address, payload);
        return true;

    case TRANSLATE_PATH_INFO:
        if (client->cgi_address == NULL) {
            /* don't emit this error when the resource is a local
               path.  This combination might be useful one day, but isn't
               currently used. */
            if (client->resource_address == NULL ||
                client->resource_address->type != RESOURCE_ADDRESS_LOCAL) {
                translate_client_error(client,
                                       "misplaced TRANSLATE_PATH_INFO packet");
                return false;
            }

            return true;
        }

        client->cgi_address->path_info = payload;
        return true;

    case TRANSLATE_EXPAND_PATH:
        if (client->response.regex == NULL) {
            translate_client_error(client,
                                   "misplaced TRANSLATE_EXPAND_PATH packet");
            return false;
        } else if (client->nfs_address != NULL &&
                   client->nfs_address->expand_path == NULL) {
            client->nfs_address->expand_path = payload;
            return true;
        } else if (client->cgi_address != NULL &&
                   client->cgi_address->expand_path == NULL) {
            client->cgi_address->expand_path = payload;
            return true;
        } else if (client->nfs_address != NULL &&
                   client->nfs_address->expand_path == NULL) {
            client->nfs_address->expand_path = payload;
            return true;
        } else if (client->file_address != NULL &&
                   client->file_address->expand_path == NULL) {
            client->file_address->expand_path = payload;
            return true;
        } else {
            translate_client_error(client,
                                   "misplaced TRANSLATE_EXPAND_PATH packet");
            return false;
        }

    case TRANSLATE_EXPAND_PATH_INFO:
        if (client->response.regex == NULL ||
            client->cgi_address == NULL ||
            client->cgi_address->expand_path_info != NULL) {
            translate_client_error(client,
                                   "misplaced TRANSLATE_EXPAND_PATH_INFO packet");
            return false;
        }

        client->cgi_address->expand_path_info = payload;
        return true;

    case TRANSLATE_DEFLATED:
        if (client->file_address == NULL) {
            translate_client_error(client,
                                   "misplaced TRANSLATE_DEFLATED packet");
            return false;
        }

        client->file_address->deflated = payload;
        return true;

    case TRANSLATE_GZIPPED:
        if (client->file_address == NULL) {
            translate_client_error(client, "misplaced TRANSLATE_GZIPPED packet");
            return false;
        }

        client->file_address->gzipped = payload;
        return true;

    case TRANSLATE_SITE:
        assert(client->resource_address != NULL);

        if (client->resource_address == &client->response.address)
            client->response.site = payload;
        else if (client->jail != NULL && client->jail->enabled)
            client->jail->site_id = payload;
        else {
            translate_client_error(client, "misplaced TRANSLATE_SITE packet");
            return false;
        }

        return true;

    case TRANSLATE_CONTENT_TYPE:
        if (client->file_address != NULL) {
            client->file_address->content_type = payload;
            return true;
        } else if (client->nfs_address != NULL) {
            client->nfs_address->content_type = payload;
            return true;
        } else {
            translate_client_error(client, "misplaced TRANSLATE_CONTENT_TYPE packet");
            return false;
        }

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
        client->resource_address->u.http = uwa =
            uri_address_parse(client->pool, payload, &error);
        if (uwa == NULL) {
            translate_client_abort(client, error);
            return false;
        }

        if (uwa->scheme != URI_SCHEME_UNIX && uwa->scheme != URI_SCHEME_HTTP) {
            translate_client_error(client, "malformed TRANSLATE_PROXY packet");
            return false;
        }

        client->address_list = &uwa->addresses;
        return true;

    case TRANSLATE_REDIRECT:
        client->response.redirect = payload;
        return true;

    case TRANSLATE_BOUNCE:
        client->response.bounce = payload;
        return true;

    case TRANSLATE_FILTER:
        transformation = translate_add_transformation(client);
        transformation->type = TRANSFORMATION_FILTER;
        transformation->u.filter.type = RESOURCE_ADDRESS_NONE;
        client->resource_address = &transformation->u.filter;
        client->jail = NULL;
        client->file_address = NULL;
        client->cgi_address = NULL;
        client->nfs_address = NULL;
        client->lhttp_address = NULL;
        client->address_list = NULL;
        return true;

    case TRANSLATE_FILTER_4XX:
        if (client->view != NULL)
            client->view->filter_4xx = true;
        else
            client->response.filter_4xx = true;
        return true;

    case TRANSLATE_PROCESS:
        transformation = translate_add_transformation(client);
        transformation->type = TRANSFORMATION_PROCESS;
        transformation->u.processor.options = PROCESSOR_REWRITE_URL;
        return true;

    case TRANSLATE_DOMAIN:
        daemon_log(2, "deprecated TRANSLATE_DOMAIN packet\n");
        return true;

    case TRANSLATE_CONTAINER:
        if (client->transformation == NULL ||
            client->transformation->type != TRANSFORMATION_PROCESS) {
            translate_client_error(client,
                                   "misplaced TRANSLATE_CONTAINER packet");
            return false;
        }

        client->transformation->u.processor.options |= PROCESSOR_CONTAINER;
        return true;

    case TRANSLATE_SELF_CONTAINER:
        if (client->transformation == NULL ||
            client->transformation->type != TRANSFORMATION_PROCESS) {
            translate_client_error(client,
                                   "misplaced TRANSLATE_SELF_CONTAINER packet");
            return false;
        }

        client->transformation->u.processor.options |=
            PROCESSOR_SELF_CONTAINER|PROCESSOR_CONTAINER;
        return true;

    case TRANSLATE_GROUP_CONTAINER:
        if (client->transformation == NULL ||
            client->transformation->type != TRANSFORMATION_PROCESS) {
            translate_client_error(client,
                                   "misplaced TRANSLATE_GROUP_CONTAINER packet");
            return false;
        }

        if (payload == NULL) {
            translate_client_error(client,
                                   "malformed TRANSLATE_GROUP_CONTAINER packet");
            return false;
        }

        if (client->transformation == NULL ||
            client->transformation->type != TRANSFORMATION_PROCESS) {
            translate_client_error(client,
                                   "misplaced TRANSLATE_GROUP_CONTAINER packet");
            return false;
        }

        client->transformation->u.processor.options |= PROCESSOR_CONTAINER;
        strset_add(client->pool, &client->response.container_groups, payload);
        return true;

    case TRANSLATE_WIDGET_GROUP:
        if (payload == NULL) {
            translate_client_error(client,
                                   "malformed TRANSLATE_WIDGET_GROUP packet");
            return false;
        }

        client->response.widget_group = payload;
        return true;

    case TRANSLATE_UNTRUSTED:
        if (*payload == 0 || *payload == '.' || payload[strlen(payload) - 1] == '.') {
            translate_client_error(client,
                                   "malformed TRANSLATE_UNTRUSTED packet");
            return false;
        }

        if (client->response.untrusted_prefix != NULL ||
            client->response.untrusted_site_suffix != NULL) {
            translate_client_error(client,
                                   "misplaced TRANSLATE_UNTRUSTED packet");
            return false;
        }

        client->response.untrusted = payload;
        return true;

    case TRANSLATE_UNTRUSTED_PREFIX:
        if (*payload == 0 || *payload == '.' || payload[strlen(payload) - 1] == '.') {
            translate_client_error(client,
                                   "malformed TRANSLATE_UNTRUSTED_PREFIX packet");
            return false;
        }

        if (client->response.untrusted != NULL ||
            client->response.untrusted_site_suffix != NULL) {
            translate_client_error(client,
                                   "misplaced TRANSLATE_UNTRUSTED_PREFIX packet");
            return false;
        }

        client->response.untrusted_prefix = payload;
        return true;

    case TRANSLATE_UNTRUSTED_SITE_SUFFIX:
        if (*payload == 0 || *payload == '.' || payload[strlen(payload) - 1] == '.') {
            daemon_log(2, "malformed TRANSLATE_UNTRUSTED_SITE_SUFFIX packet\n");
            return false;
        }

        if (client->response.untrusted != NULL ||
            client->response.untrusted_prefix != NULL) {
            daemon_log(2, "misplaced TRANSLATE_UNTRUSTED_SITE_SUFFIX packet\n");
            return false;
        }

        client->response.untrusted_site_suffix = payload;
        return true;

    case TRANSLATE_SCHEME:
        if (strncmp(payload, "http", 4) != 0) {
            translate_client_error(client,
                                   "misplaced TRANSLATE_SCHEME packet");
            return false;
        }

        client->response.scheme = payload;
        return true;

    case TRANSLATE_HOST:
        client->response.host = payload;
        return true;

    case TRANSLATE_URI:
        if (payload == NULL || *payload != '/') {
            translate_client_error(client, "malformed TRANSLATE_URI packet");
            return false;
        }

        client->response.uri = payload;
        return true;

    case TRANSLATE_DIRECT_ADDRESSING:
        client->response.direct_addressing = true;
        return true;

    case TRANSLATE_STATEFUL:
        client->response.stateful = true;
        return true;

    case TRANSLATE_SESSION:
        client->response.session = payload;
        return true;

    case TRANSLATE_USER:
        client->response.user = payload;
        client->previous_command = command;
        return true;

    case TRANSLATE_REALM:
        client->response.realm = payload;
        return true;

    case TRANSLATE_LANGUAGE:
        client->response.language = payload;
        return true;

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
        client->resource_address->u.cgi = client->cgi_address =
            cgi_address_new(client->pool, payload, false);

        client->jail = &client->cgi_address->jail;
        return true;

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
        client->resource_address->u.cgi = client->cgi_address =
            cgi_address_new(client->pool, payload, false);

        client->cgi_address->document_root = client->response.document_root;
        client->jail = &client->cgi_address->jail;
        return true;

    case TRANSLATE_FASTCGI:
        if (client->resource_address == NULL ||
            client->resource_address->type != RESOURCE_ADDRESS_NONE) {
            translate_client_error(client,
                                   "misplaced TRANSLATE_FASTCGI packet");
            return false;
        }

        if (payload == NULL) {
            translate_client_error(client,
                                   "malformed TRANSLATE_FASTCGI packet");
            return false;
        }

        client->resource_address->type = RESOURCE_ADDRESS_FASTCGI;
        client->resource_address->u.cgi = client->cgi_address =
            cgi_address_new(client->pool, payload, true);

        client->jail = &client->cgi_address->jail;
        client->address_list = &client->cgi_address->address_list;
        return true;

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
        client->resource_address->u.http = uwa =
            uri_address_parse(client->pool, payload, &error);
        if (uwa == NULL) {
            translate_client_abort(client, error);
            return false;
        }

        if (uwa->scheme != URI_SCHEME_AJP) {
            translate_client_error(client, "malformed TRANSLATE_AJP packet");
            return false;
        }

        client->address_list = &uwa->addresses;
        return true;

    case TRANSLATE_NFS_SERVER:
        if (client->resource_address == NULL ||
            client->resource_address->type != RESOURCE_ADDRESS_NONE) {
            translate_client_error(client, "misplaced TRANSLATE_NFS_SERVER packet");
            return false;
        }

        if (payload == NULL) {
            translate_client_error(client, "malformed TRANSLATE_NFS_SERVER packet");
            return false;
        }

        client->resource_address->type = RESOURCE_ADDRESS_NFS;
        client->resource_address->u.nfs = client->nfs_address =
            nfs_address_new(client->pool, payload, "", "");
        return true;

    case TRANSLATE_NFS_EXPORT:
        if (client->nfs_address == NULL ||
            *client->nfs_address->export != 0) {
            translate_client_error(client, "misplaced TRANSLATE_NFS_EXPORT packet");
            return false;
        }

        if (payload == NULL || *payload != '/') {
            translate_client_error(client, "malformed TRANSLATE_NFS_EXPORT packet");
            return false;
        }

        client->nfs_address->export = payload;
        return true;

    case TRANSLATE_JAILCGI:
        if (client->jail == NULL) {
            translate_client_error(client,
                                   "misplaced TRANSLATE_JAILCGI packet");
            return false;
        }

        client->jail->enabled = true;
        return true;

    case TRANSLATE_HOME:
        if (client->jail == NULL || !client->jail->enabled ||
            client->jail->home_directory != NULL) {
            translate_client_error(client,
                                   "misplaced TRANSLATE_HOME packet");
            return false;
        }

        client->jail->home_directory = payload;
        return true;

    case TRANSLATE_INTERPRETER:
        if (client->resource_address == NULL ||
            (client->resource_address->type != RESOURCE_ADDRESS_CGI &&
             client->resource_address->type != RESOURCE_ADDRESS_FASTCGI) ||
            client->cgi_address->interpreter != NULL) {
            translate_client_error(client,
                                   "misplaced TRANSLATE_INTERPRETER packet");
            return false;
        }

        client->cgi_address->interpreter = payload;
        return true;

    case TRANSLATE_ACTION:
        if (client->resource_address == NULL ||
            (client->resource_address->type != RESOURCE_ADDRESS_CGI &&
             client->resource_address->type != RESOURCE_ADDRESS_FASTCGI) ||
            client->cgi_address->action != NULL) {
            translate_client_error(client,
                                   "misplaced TRANSLATE_ACTION packet");
            return false;
        }

        client->cgi_address->action = payload;
        return true;

    case TRANSLATE_SCRIPT_NAME:
        if (client->resource_address == NULL ||
            (client->resource_address->type != RESOURCE_ADDRESS_CGI &&
             client->resource_address->type != RESOURCE_ADDRESS_WAS &&
             client->resource_address->type != RESOURCE_ADDRESS_FASTCGI) ||
            client->cgi_address->script_name != NULL) {
            translate_client_error(client,
                                   "misplaced TRANSLATE_SCRIPT_NAME packet");
            return false;
        }

        client->cgi_address->script_name = payload;
        return true;

    case TRANSLATE_DOCUMENT_ROOT:
        if (client->cgi_address != NULL)
            client->cgi_address->document_root = payload;
        else if (client->file_address != NULL &&
                 client->file_address->delegate != NULL)
            client->file_address->document_root = payload;
        else
            client->response.document_root = payload;
        return true;

    case TRANSLATE_ADDRESS:
        if (client->address_list == NULL) {
            translate_client_error(client,
                                   "misplaced TRANSLATE_ADDRESS packet");
            return false;
        }

        if (payload_length < 2) {
            translate_client_error(client,
                                   "malformed TRANSLATE_INTERPRETER packet");
            return false;
        }

        address_list_add(client->pool, client->address_list,
                         (const struct sockaddr *)payload, payload_length);
        return true;


    case TRANSLATE_ADDRESS_STRING:
        if (client->address_list == NULL) {
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

            ret = parse_address_string(client->pool, client->address_list,
                                       payload);
            if (!ret) {
                translate_client_error(client,
                                       "malformed TRANSLATE_ADDRESS_STRING packet");
                return false;
            }
        }

        return true;

    case TRANSLATE_VIEW:
        if (!valid_view_name(payload)) {
            translate_client_error(client, "invalid view name");
            return false;
        }

        add_view(client, payload);
        return true;

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

        return true;

    case TRANSLATE_VARY:
        if (payload_length == 0 ||
            payload_length % sizeof(client->response.vary[0]) != 0) {
            translate_client_error(client, "malformed TRANSLATE_VARY packet");
            return false;
        }

        client->response.vary = (const uint16_t *)payload;
        client->response.num_vary = payload_length / sizeof(client->response.vary[0]);
        return true;

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
        return true;

    case TRANSLATE_BASE:
        client->response.base = payload;
        return true;

    case TRANSLATE_REGEX:
        if (client->response.base == NULL) {
            translate_client_error(client, "REGEX without BASE");
            return false;
        }

        client->response.regex = payload;
        return true;

    case TRANSLATE_INVERSE_REGEX:
        if (client->response.base == NULL) {
            translate_client_error(client, "INVERSE_REGEX without BASE");
            return false;
        }

        client->response.inverse_regex = payload;
        return true;

    case TRANSLATE_DELEGATE:
        if (client->file_address == NULL) {
            translate_client_error(client,
                                   "misplaced TRANSLATE_DELEGATE packet");
            return false;
        }

        if (payload == NULL) {
            translate_client_error(client,
                                   "malformed TRANSLATE_DELEGATE packet");
            return false;
        }

        client->file_address->delegate = payload;
        client->jail = &client->file_address->jail;
        return true;

    case TRANSLATE_APPEND:
        if (payload == NULL) {
            translate_client_error(client,
                                   "malformed TRANSLATE_APPEND packet");
            return false;
        }

        if (client->resource_address == NULL) {
            translate_client_error(client,
                                   "misplaced TRANSLATE_APPEND packet");
            return false;
        }

        if (client->resource_address->type == RESOURCE_ADDRESS_PIPE) {
            if (client->cgi_address->num_args >=
                G_N_ELEMENTS(client->cgi_address->args)) {
                translate_client_error(client,
                                       "too many TRANSLATE_APPEND packets");
                return false;
            }

            client->cgi_address->args[client->cgi_address->num_args++] = payload;
            return true;
        } else if (client->lhttp_address != NULL) {
            if (client->lhttp_address->num_args >=
                G_N_ELEMENTS(client->lhttp_address->args)) {
                translate_client_error(client,
                                       "too many TRANSLATE_APPEND packets");
                return false;
            }

            client->lhttp_address->args[client->lhttp_address->num_args++] = payload;
            return true;
        } else {
            translate_client_error(client,
                                   "misplaced TRANSLATE_APPEND packet");
            return false;
        }

    case TRANSLATE_PAIR:
        if (client->cgi_address != NULL) {
            if (client->cgi_address->num_args >=
                G_N_ELEMENTS(client->cgi_address->args)) {
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

            client->cgi_address->args[client->cgi_address->num_args++] = payload;
        } else {
            translate_client_error(client,
                                   "misplaced TRANSLATE_PAIR packet");
            return false;
        }

        return true;

    case TRANSLATE_DISCARD_SESSION:
        client->response.discard_session = true;
        return true;

    case TRANSLATE_REQUEST_HEADER_FORWARD:
        if (client->view != NULL)
            parse_header_forward(&client->view->request_header_forward,
                                 payload, payload_length);
        else
            parse_header_forward(&client->response.request_header_forward,
                                 payload, payload_length);
        return true;

    case TRANSLATE_RESPONSE_HEADER_FORWARD:
        if (client->view != NULL)
            parse_header_forward(&client->view->response_header_forward,
                                 payload, payload_length);
        else
            parse_header_forward(&client->response.response_header_forward,
                                 payload, payload_length);
        return true;

    case TRANSLATE_WWW_AUTHENTICATE:
        client->response.www_authenticate = payload;
        return true;

    case TRANSLATE_AUTHENTICATION_INFO:
        client->response.authentication_info = payload;
        return true;

    case TRANSLATE_HEADER:
        if (!parse_header(client->pool, &client->response,
                          payload, payload_length, &error)) {
            translate_client_abort(client, error);
            return false;
        }

        return true;

    case TRANSLATE_SECURE_COOKIE:
        client->response.secure_cookie = true;
        return true;

    case TRANSLATE_COOKIE_DOMAIN:
        if (client->response.cookie_domain != NULL) {
            translate_client_error(client,
                                   "misplaced TRANSLATE_COOKIE_DOMAIN packet");
            return false;
        }

        if (payload == NULL) {
            translate_client_error(client,
                                   "malformed TRANSLATE_COOKIE_DOMAIN packet");
            return false;
        }

        client->response.cookie_domain = payload;
        return true;

    case TRANSLATE_ERROR_DOCUMENT:
        client->response.error_document = true;
        return true;

    case TRANSLATE_CHECK:
        if (payload != NULL)
            strref_set(&client->response.check, payload, payload_length);
        else
            strref_set(&client->response.check, "", 0);
        return true;

    case TRANSLATE_PREVIOUS:
        client->response.previous = true;
        return true;

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
        client->resource_address->u.cgi = client->cgi_address =
            cgi_address_new(client->pool, payload, false);

        client->jail = &client->cgi_address->jail;
        return true;

    case TRANSLATE_TRANSPARENT:
        client->response.transparent = true;
        return true;

    case TRANSLATE_WIDGET_INFO:
        client->response.widget_info = true;
        return true;

    case TRANSLATE_STICKY:
        if (client->address_list == NULL) {
            translate_client_error(client,
                                   "misplaced TRANSLATE_STICKY packet");
            return false;
        }

        address_list_set_sticky_mode(client->address_list,
                                     STICKY_SESSION_MODULO);
        return true;

    case TRANSLATE_DUMP_HEADERS:
        client->response.dump_headers = true;
        return true;

    case TRANSLATE_COOKIE_HOST:
        if (client->resource_address == NULL ||
            client->resource_address->type == RESOURCE_ADDRESS_NONE) {
            translate_client_error(client,
                                   "misplaced TRANSLATE_COOKIE_HOST packet");
            return false;
        }

        if (payload == NULL) {
            translate_client_error(client,
                                   "malformed TRANSLATE_COOKIE_HOST packet");
            return false;
        }

        client->response.cookie_host = payload;
        return true;

    case TRANSLATE_PROCESS_CSS:
        transformation = translate_add_transformation(client);
        transformation->type = TRANSFORMATION_PROCESS_CSS;
        transformation->u.css_processor.options = CSS_PROCESSOR_REWRITE_URL;
        return true;

    case TRANSLATE_PREFIX_CSS_CLASS:
        if (client->transformation == NULL) {
            translate_client_error(client,
                                   "misplaced TRANSLATE_PREFIX_CSS_CLASS packet");
            return false;
        }

        switch (client->transformation->type) {
        case TRANSFORMATION_PROCESS:
            client->transformation->u.processor.options |= PROCESSOR_PREFIX_CSS_CLASS;
            break;

        case TRANSFORMATION_PROCESS_CSS:
            client->transformation->u.css_processor.options |= CSS_PROCESSOR_PREFIX_CLASS;
            break;

        default:
            translate_client_error(client,
                                   "misplaced TRANSLATE_PREFIX_CSS_CLASS packet");
            return false;
        }

        return true;

    case TRANSLATE_PREFIX_XML_ID:
        if (client->transformation == NULL) {
            translate_client_error(client,
                                   "misplaced TRANSLATE_PREFIX_XML_ID packet");
            return false;
        }

        switch (client->transformation->type) {
        case TRANSFORMATION_PROCESS:
            client->transformation->u.processor.options |= PROCESSOR_PREFIX_XML_ID;
            break;

        case TRANSFORMATION_PROCESS_CSS:
            client->transformation->u.css_processor.options |= CSS_PROCESSOR_PREFIX_ID;
            break;

        default:
            translate_client_error(client,
                                   "misplaced TRANSLATE_PREFIX_XML_ID packet");
            return false;
        }

        return true;

    case TRANSLATE_PROCESS_STYLE:
        if (client->transformation == NULL ||
            client->transformation->type != TRANSFORMATION_PROCESS) {
            translate_client_error(client,
                                   "misplaced TRANSLATE_PROCESS_STYLE packet");
            return false;
        }

        client->transformation->u.processor.options |= PROCESSOR_STYLE;
        return true;

    case TRANSLATE_FOCUS_WIDGET:
        if (client->transformation == NULL ||
            client->transformation->type != TRANSFORMATION_PROCESS) {
            translate_client_error(client,
                                   "misplaced TRANSLATE_FOCUS_WIDGET packet");
            return false;
        }

        client->transformation->u.processor.options |= PROCESSOR_FOCUS_WIDGET;
        return true;

    case TRANSLATE_ANCHOR_ABSOLUTE:
        if (client->transformation == NULL ||
            client->transformation->type != TRANSFORMATION_PROCESS) {
            translate_client_error(client,
                                   "misplaced TRANSLATE_ANCHOR_ABSOLUTE packet");
            return false;
        }

        client->response.anchor_absolute = true;
        return true;

    case TRANSLATE_PROCESS_TEXT:
        transformation = translate_add_transformation(client);
        transformation->type = TRANSFORMATION_PROCESS_TEXT;
        return true;

    case TRANSLATE_LOCAL_URI:
        if (client->response.local_uri != NULL) {
            translate_client_error(client,
                                   "misplaced TRANSLATE_LOCAL_URI packet");
            return false;
        }

        if (payload == NULL || *payload == 0 ||
            payload[strlen(payload) - 1] != '/') {
            translate_client_error(client,
                                   "malformed TRANSLATE_LOCAL_URI packet");
            return false;
        }

        client->response.local_uri = payload;
        return true;

    case TRANSLATE_AUTO_BASE:
        if (client->resource_address != &client->response.address ||
            client->cgi_address != client->response.address.u.cgi ||
            client->cgi_address->path_info == NULL ||
            client->response.auto_base) {
            translate_client_error(client,
                                   "misplaced TRANSLATE_AUTO_BASE packet");
            return false;
        }

        client->response.auto_base = true;
        return true;

    case TRANSLATE_VALIDATE_MTIME:
        if (payload_length < 10 || payload[8] != '/' ||
            memchr(payload + 9, 0, payload_length - 9) != NULL) {
            translate_client_error(client,
                                   "malformed TRANSLATE_VALIDATE_MTIME packet");
            return false;
        }

        client->response.validate_mtime.mtime = *(const uint64_t *)payload;
        client->response.validate_mtime.path =
            p_strndup(client->pool, payload + 8, payload_length - 8);
        return true;

    case TRANSLATE_LHTTP_PATH:
        if (client->resource_address == NULL ||
            client->resource_address->type != RESOURCE_ADDRESS_NONE) {
            translate_client_error(client, "misplaced TRANSLATE_LHTTP_PATH packet");
            return false;
        }

        if (payload == NULL || *payload != '/') {
            translate_client_error(client, "malformed TRANSLATE_LHTTP_PATH packet");
            return false;
        }

        client->resource_address->type = RESOURCE_ADDRESS_LHTTP;
        client->resource_address->u.lhttp = client->lhttp_address =
            lhttp_address_new(client->pool, payload);
        client->jail = &client->lhttp_address->jail;
        return true;

    case TRANSLATE_LHTTP_URI:
        if (client->lhttp_address == NULL ||
            client->lhttp_address->uri != NULL) {
            translate_client_error(client,
                                   "misplaced TRANSLATE_LHTTP_HOST packet");
            return false;
        }

        if (payload == NULL || *payload != '/') {
            translate_client_error(client, "malformed TRANSLATE_LHTTP_URI packet");
            return false;
        }

        client->lhttp_address->uri = payload;
        return true;

    case TRANSLATE_LHTTP_EXPAND_URI:
        if (client->lhttp_address == NULL ||
            client->lhttp_address->uri == NULL ||
            client->lhttp_address->expand_uri != NULL ||
            client->response.regex == NULL) {
            translate_client_error(client,
                                   "misplaced TRANSLATE_LHTTP_EXPAND_URI packet");
            return false;
        }

        client->lhttp_address->expand_uri = payload;
        return true;

    case TRANSLATE_LHTTP_HOST:
        if (client->lhttp_address == NULL ||
            client->lhttp_address->host_and_port != NULL) {
            translate_client_error(client,
                                   "misplaced TRANSLATE_LHTTP_HOST packet");
            return false;
        }

        if (payload == NULL) {
            translate_client_error(client, "malformed TRANSLATE_LHTTP_HOST packet");
            return false;
        }

        client->lhttp_address->host_and_port = payload;
        return true;
    }

    error = g_error_new(translate_quark(), 0,
                        "unknown translation packet: %u", command);
    translate_client_abort(client, error);
    return false;
}

static enum buffered_result
translate_client_feed(struct translate_client *client,
                      const uint8_t *data, size_t length)
{
    size_t consumed = 0;
    while (consumed < length) {
        size_t nbytes = packet_reader_feed(client->pool, &client->reader,
                                           data + consumed, length - consumed);
        if (nbytes == 0)
            /* need more data */
            break;

        consumed += nbytes;
        buffered_socket_consumed(&client->socket, nbytes);

        if (client->reader.state != PACKET_READER_COMPLETE)
            /* need more data */
            break;

        if (!translate_handle_packet(client,
                                     client->reader.header.command,
                                     client->reader.payload == NULL
                                     ? "" : client->reader.payload,
                                     client->reader.header.length))
            return BUFFERED_CLOSED;
    }

    return BUFFERED_MORE;
}

/*
 * send requests
 *
 */

static bool
translate_try_write(struct translate_client *client)
{
    size_t length;
    const void *data = growing_buffer_reader_read(&client->request, &length);
    assert(data != NULL);

    ssize_t nbytes = buffered_socket_write(&client->socket, data, length);
    if (gcc_unlikely(nbytes < 0)) {
        if (gcc_likely(nbytes == WRITE_BLOCKING))
            return true;

        GError *error =
            new_error_errno_msg("write error to translation server");
        translate_client_abort(client, error);
        return false;
    }

    growing_buffer_reader_consume(&client->request, nbytes);
    if (growing_buffer_reader_eof(&client->request)) {
        /* the buffer is empty, i.e. the request has been sent */

        stopwatch_event(client->stopwatch, "request");

        buffered_socket_unschedule_write(&client->socket);

        packet_reader_init(&client->reader);
        return buffered_socket_read(&client->socket, true);
    }

    buffered_socket_schedule_write(&client->socket);
    return true;
}


/*
 * buffered_socket handler
 *
 */

static enum buffered_result
translate_client_socket_data(const void *buffer, size_t size, void *ctx)
{
    struct translate_client *client = ctx;

    return translate_client_feed(client, buffer, size);
}

static bool
translate_client_socket_closed(void *ctx)
{
    struct translate_client *client = ctx;

    translate_client_release_socket(client, false);
    return true;
}

static bool
translate_client_socket_write(void *ctx)
{
    struct translate_client *client = ctx;

    return translate_try_write(client);
}

static void
translate_client_socket_error(GError *error, void *ctx)
{
    struct translate_client *client = ctx;

    g_prefix_error(&error, "Translation server connection failed: ");
    translate_client_abort(client, error);
}

static const struct buffered_socket_handler translate_client_socket_handler = {
    .data = translate_client_socket_data,
    .closed = translate_client_socket_closed,
    .write = translate_client_socket_write,
    .error = translate_client_socket_error,
};

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
translate(struct pool *pool, int fd,
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
    buffered_socket_init(&client->socket, pool, fd, ISTREAM_SOCKET,
                         &translate_read_timeout,
                         &translate_write_timeout,
                         &translate_client_socket_handler, client);
    p_lease_ref_set(&client->lease_ref, lease, lease_ctx,
                    pool, "translate_lease");

    growing_buffer_reader_init(&client->request, gb);
    client->handler = handler;
    client->handler_ctx = ctx;
    client->response.status = (http_status_t)-1;

    async_init(&client->async, &translate_operation);
    async_ref_set(async_ref, &client->async);

    pool_ref(client->pool);
    translate_try_write(client);
}
