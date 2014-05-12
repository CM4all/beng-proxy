/*
 * Call the translation server.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "translate_client.hxx"
#include "translate_request.hxx"
#include "translate_response.hxx"
#include "buffered_socket.hxx"
#include "transformation.hxx"
#include "widget_class.hxx"
#include "please.h"
#include "growing-buffer.h"
#include "processor.h"
#include "css_processor.h"
#include "async.h"
#include "file_address.hxx"
#include "lhttp_address.hxx"
#include "http_address.hxx"
#include "cgi_address.hxx"
#include "nfs_address.h"
#include "mount_list.h"
#include "strutil.h"
#include "strmap.h"
#include "stopwatch.h"
#include "beng-proxy/translation.h"
#include "gerrno.h"
#include "util/Cast.hxx"

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
#include <netdb.h>

static const uint8_t PROTOCOL_VERSION = 1;

struct TranslatePacketReader {
    enum class State {
        HEADER,
        PAYLOAD,
        COMPLETE,
    };

    State state;

    struct beng_translation_header header;

    char *payload;
    size_t payload_position;
};

struct TranslateClient {
    struct pool *pool;

    struct stopwatch *stopwatch;

    BufferedSocket socket;
    struct lease_ref lease_ref;

    struct {
        const char *uri;

        bool want_full_uri;

        bool want;

        bool content_type_lookup;
    } from_request;

    /** the marshalled translate request */
    struct growing_buffer_reader request;

    const TranslateHandler *handler;
    void *handler_ctx;

    TranslatePacketReader reader;
    TranslateResponse response;

    enum beng_translation_command previous_command;

    /** the current resource address being edited */
    struct resource_address *resource_address;

    /** the current JailCGI parameters being edited */
    struct jail_params *jail;

    /** the current child process options being edited */
    struct child_options *child_options;

    /** the current namespace options being edited */
    struct namespace_options *ns_options;

    /** the tail of the current mount_list */
    struct mount_list **mount_list;

    /** the current local file address being edited */
    struct file_address *file_address;

    /** the current HTTP/AJP address being edited */
    struct http_address *http_address;

    /** the current CGI/FastCGI/WAS address being edited */
    struct cgi_address *cgi_address;

    /** the current NFS address being edited */
    struct nfs_address *nfs_address;

    /** the current "local HTTP" address being edited */
    struct lhttp_address *lhttp_address;

    /** the current address list being edited */
    struct address_list *address_list;

    /**
     * Default port for #TRANSLATE_ADDRESS_STRING.
     */
    int default_port;

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

    static TranslateClient *FromAsync(async_operation *ao) {
        return ContainerCast(ao, TranslateClient, async);
    }
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
translate_client_release_socket(TranslateClient *client, bool reuse)
{
    assert(client != nullptr);
    assert(client->socket.IsConnected());

    stopwatch_dump(client->stopwatch);

    client->socket.Abandon();
    client->socket.Destroy();

    p_lease_release(&client->lease_ref, reuse, client->pool);
}

/**
 * Release resources held by this object: the event object, the socket
 * lease, and the pool reference.
 */
static void
translate_client_release(TranslateClient *client, bool reuse)
{
    assert(client != nullptr);

    translate_client_release_socket(client, reuse);
    pool_unref(client->pool);
}

static void
translate_client_abort(TranslateClient *client, GError *error)
{
    stopwatch_event(client->stopwatch, "error");

    translate_client_release_socket(client, false);

    async_operation_finished(&client->async);
    client->handler->error(error, client->handler_ctx);
    pool_unref(client->pool);
}

static void
translate_client_error(TranslateClient *client, const char *msg)
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
                          payload != nullptr ? strlen(payload) : 0,
                          error_r);
}

template<typename T>
static bool
write_buffer(growing_buffer *gb, uint16_t command,
             ConstBuffer<T> buffer, GError **error_r)
{
    auto b = buffer.ToVoid();
    return write_packet_n(gb, command, b.data, b.size, error_r);
}

/**
 * Forward the command to write_packet() only if #payload is not nullptr.
 */
static bool
write_optional_packet(struct growing_buffer *gb, uint16_t command,
                      const char *payload, GError **error_r)
{
    if (payload == nullptr)
        return true;

    return write_packet(gb, command, payload, error_r);
}

template<typename T>
static bool
write_optional_buffer(growing_buffer *gb, uint16_t command,
                      ConstBuffer<T> buffer, GError **error_r)
{
    return buffer.IsNull() || write_buffer(gb, command, buffer, error_r);
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

    assert(address != nullptr);
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
    assert((address == nullptr) == (address_length == 0));

    return address != nullptr
        ? write_sockaddr(gb, command, command_string, address, address_length,
                         error_r)
        : true;
}

static struct growing_buffer *
marshal_request(struct pool *pool, const TranslateRequest *request,
                GError **error_r)
{
    struct growing_buffer *gb;
    bool success;

    gb = growing_buffer_new(pool, 512);

    success = write_packet_n(gb, TRANSLATE_BEGIN,
                             &PROTOCOL_VERSION, sizeof(PROTOCOL_VERSION),
                             error_r) &&
        write_optional_buffer(gb, TRANSLATE_ERROR_DOCUMENT,
                              request->error_document,
                              error_r) &&
        (request->error_document_status == 0 ||
         write_short(gb, TRANSLATE_STATUS,
                     request->error_document_status, error_r)) &&
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
        write_optional_buffer(gb, TRANSLATE_SESSION, request->session,
                              error_r) &&
        write_optional_buffer(gb, TRANSLATE_CHECK, request->check,
                              error_r) &&
        write_optional_buffer(gb, TRANSLATE_WANT_FULL_URI,
                              request->want_full_uri, error_r) &&
        write_optional_buffer(gb, TRANSLATE_WANT, request->want, error_r) &&
        write_optional_buffer(gb, TRANSLATE_FILE_NOT_FOUND,
                              request->file_not_found, error_r) &&
        write_optional_buffer(gb, TRANSLATE_CONTENT_TYPE_LOOKUP,
                              request->content_type_lookup, error_r) &&
        write_optional_packet(gb, TRANSLATE_SUFFIX, request->suffix,
                              error_r) &&
        write_optional_buffer(gb, TRANSLATE_ENOTDIR,
                              request->enotdir, error_r) &&
        write_optional_buffer(gb, TRANSLATE_DIRECTORY_INDEX,
                              request->directory_index, error_r) &&
        write_optional_packet(gb, TRANSLATE_PARAM, request->param,
                              error_r) &&
        write_packet(gb, TRANSLATE_END, nullptr, error_r);
    if (!success)
        return nullptr;

    return gb;
}


/*
 * packet reader
 *
 */

static void
packet_reader_init(TranslatePacketReader *reader)
{
    reader->state = TranslatePacketReader::State::HEADER;
}

/**
 * Read a packet from the socket.
 *
 * @return the number of bytes consumed
 */
static size_t
packet_reader_feed(struct pool *pool, TranslatePacketReader *reader,
                   const uint8_t *data, size_t length)
{
    assert(reader->state == TranslatePacketReader::State::HEADER ||
           reader->state == TranslatePacketReader::State::PAYLOAD ||
           reader->state == TranslatePacketReader::State::COMPLETE);

    /* discard the packet that was completed (and consumed) by the
       previous call */
    if (reader->state == TranslatePacketReader::State::COMPLETE)
        reader->state = TranslatePacketReader::State::HEADER;

    size_t consumed = 0;

    if (reader->state == TranslatePacketReader::State::HEADER) {
        if (length < sizeof(reader->header))
            /* need more data */
            return 0;

        memcpy(&reader->header, data, sizeof(reader->header));

        if (reader->header.length == 0) {
            reader->payload = nullptr;
            reader->state = TranslatePacketReader::State::COMPLETE;
            return sizeof(reader->header);
        }

        consumed += sizeof(reader->header);
        data += sizeof(reader->header);
        length -= sizeof(reader->header);

        reader->state = TranslatePacketReader::State::PAYLOAD;

        reader->payload_position = 0;
        reader->payload = PoolAlloc<char>(pool, reader->header.length + 1);
        reader->payload[reader->header.length] = 0;

        if (length == 0)
            return consumed;
    }

    assert(reader->state == TranslatePacketReader::State::PAYLOAD);

    assert(reader->payload_position < reader->header.length);

    size_t nbytes = reader->header.length - reader->payload_position;
    if (nbytes > length)
        nbytes = length;

    memcpy(reader->payload + reader->payload_position, data, nbytes);
    reader->payload_position += nbytes;
    if (reader->payload_position == reader->header.length)
        reader->state = TranslatePacketReader::State::COMPLETE;

    consumed += nbytes;
    return consumed;
}


/*
 * receive response
 *
 */

gcc_pure
static bool
has_null_byte(const void *p, size_t size)
{
    return memchr(p, 0, size) != nullptr;
}

gcc_pure
static bool
has_null_byte(ConstBuffer<void> buffer)
{
    return has_null_byte(buffer.data, buffer.size);
}

static struct transformation *
translate_add_transformation(TranslateClient *client)
{
    transformation *transformation = (struct transformation *)
        p_malloc(client->pool, sizeof(*transformation));

    transformation->next = nullptr;
    client->transformation = transformation;
    *client->transformation_tail = transformation;
    client->transformation_tail = &transformation->next;

    return transformation;
}

static bool
parse_address_string(struct pool *pool, struct address_list *list,
                     const char *p, int default_port)
{
    if (*p == '/' || *p == '@') {
        /* unix domain socket */

        struct sockaddr_un sun;
        size_t path_length = strlen(p);

        if (path_length >= sizeof(sun.sun_path))
            return false;

        sun.sun_family = AF_UNIX;
        memcpy(sun.sun_path, p, path_length + 1);

        size_t size = SUN_LEN(&sun);

        if (*p == '@')
            /* abstract socket */
            sun.sun_path[0] = 0;

        address_list_add(pool, list,
                         (const struct sockaddr *)&sun, size);
        return true;
    }

    struct addrinfo hints, *ai;
    int ret;

    memset(&hints, 0, sizeof(hints));
    hints.ai_flags = AI_NUMERICHOST;
    hints.ai_socktype = SOCK_STREAM;

    ret = socket_resolve_host_port(p, default_port, &hints, &ai);
    if (ret != 0)
        return false;

    for (const struct addrinfo *i = ai; i != nullptr; i = i->ai_next)
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
    assert(name != nullptr);

    do {
        if (!valid_view_name_char(*name))
            return false;
    } while (*++name != 0);

    return true;
}

static bool
finish_lhttp_address(const struct lhttp_address &address, GError **error_r)
{
    if (address.uri == nullptr) {
        g_set_error_literal(error_r, translate_quark(), 0,
                            "missing LHTTP_URI");
        return false;
    }

    return true;
}

static bool
finish_nfs_address(const struct nfs_address &address, GError **error_r)
{
    if (address.export_name == nullptr || *address.export_name == 0) {
        g_set_error_literal(error_r, translate_quark(), 0,
                            "missing NFS_EXPORT");
        return false;
    }

    if (address.path == nullptr || *address.path == 0) {
        g_set_error_literal(error_r, translate_quark(), 0,
                            "missing NFS PATH");
        return false;
    }

    return true;
}

static bool
finish_resource_address(const struct resource_address &address,
                        GError **error_r)
{
    switch (address.type) {
    case RESOURCE_ADDRESS_NONE:
    case RESOURCE_ADDRESS_LOCAL:
    case RESOURCE_ADDRESS_HTTP:
        return true;

    case RESOURCE_ADDRESS_LHTTP:
        return finish_lhttp_address(*address.u.lhttp, error_r);

    case RESOURCE_ADDRESS_PIPE:
    case RESOURCE_ADDRESS_CGI:
    case RESOURCE_ADDRESS_FASTCGI:
    case RESOURCE_ADDRESS_WAS:
    case RESOURCE_ADDRESS_AJP:
        return true;

    case RESOURCE_ADDRESS_NFS:
        return finish_nfs_address(*address.u.nfs, error_r);
    }
}

/**
 * Finish the settings in the current view, i.e. copy attributes from
 * the "parent" view.
 */
static bool
finish_view(TranslateClient *client, GError **error_r)
{
    assert(client != nullptr);
    assert(client->response.views != nullptr);

    struct widget_view *view = client->view;
    if (client->view == nullptr) {
        view = client->response.views;
        assert(view != nullptr);

        const struct resource_address *address = &view->address;
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

    if (!finish_resource_address(view->address, error_r))
        return false;

    return true;
}

static bool
add_view(TranslateClient *client, const char *name, GError **error_r)
{
    if (!finish_view(client, error_r))
        return false;

    widget_view *view = (widget_view *)p_malloc(client->pool, sizeof(*view));
    widget_view_init(view);
    view->name = name;
    view->request_header_forward = client->response.request_header_forward;
    view->response_header_forward = client->response.response_header_forward;

    client->view = view;
    *client->widget_view_tail = view;
    client->widget_view_tail = &view->next;
    client->resource_address = &view->address;
    client->jail = nullptr;
    client->child_options = nullptr;
    client->ns_options = nullptr;
    client->mount_list = nullptr;
    client->file_address = nullptr;
    client->http_address = nullptr;
    client->cgi_address = nullptr;
    client->nfs_address = nullptr;
    client->lhttp_address = nullptr;
    client->address_list = nullptr;
    client->transformation_tail = &view->transformation;
    client->transformation = nullptr;

    return true;
}

static bool
parse_header_forward(struct header_forward_settings *settings,
                     const void *payload, size_t payload_length)
{
    const beng_header_forward_packet *packet =
        (const beng_header_forward_packet *)payload;

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

        if (packet->group == HEADER_GROUP_ALL) {
            for (unsigned i = 0; i < HEADER_GROUP_MAX; ++i)
                if (i != HEADER_GROUP_SECURE)
                    settings->modes[i] = beng_header_forward_mode(packet->mode);
        } else
            settings->modes[packet->group] = beng_header_forward_mode(packet->mode);

        ++packet;
        payload_length -= sizeof(*packet);
    }

    return true;
}

static bool
parse_header(struct pool *pool, TranslateResponse *response,
             const char *payload, size_t payload_length,
             GError **error_r)
{
    const char *value = (const char *)memchr(payload, ':', payload_length);
    if (value == nullptr || value == payload ||
        has_null_byte(payload, payload_length)) {
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

    if (response->headers == nullptr)
        response->headers = strmap_new(pool, 17);
    strmap_add(response->headers, name, value);

    return true;
}

static bool
translate_jail_finish(struct jail_params *jail,
                      const TranslateResponse *response,
                      const char *document_root,
                      GError **error_r)
{
    if (!jail->enabled)
        return true;

    if (jail->home_directory == nullptr)
        jail->home_directory = document_root;

    if (jail->home_directory == nullptr) {
        g_set_error(error_r, translate_quark(), 0,
                    "No home directory for JAIL");
        return false;
    }

    if (jail->site_id == nullptr)
        jail->site_id = response->site;

    return true;
}

/**
 * Final fixups for the response before it is passed to the handler.
 */
static bool
translate_response_finish(TranslateResponse *response,
                          GError **error_r)
{
    if (!finish_resource_address(response->address, error_r))
        return false;

    if (resource_address_is_cgi_alike(&response->address)) {
        struct cgi_address *cgi = resource_address_get_cgi(&response->address);

        if (cgi->uri == nullptr)
            cgi->uri = response->uri;

        if (cgi->document_root == nullptr)
            cgi->document_root = response->document_root;

        if (!translate_jail_finish(&cgi->options.jail,
                                   response, cgi->document_root,
                                   error_r))
            return false;
    } else if (response->address.type == RESOURCE_ADDRESS_LOCAL) {
        struct file_address *file = resource_address_get_file(&response->address);

        if (file->child_options.jail.enabled && file->document_root == nullptr)
            file->document_root = response->document_root;

        if (!translate_jail_finish(&file->child_options.jail, response,
                                   file->document_root,
                                   error_r))
            return false;
    }

    return true;
}

static bool
translate_client_pivot_root(TranslateClient *client,
                            const char *payload)
{
    if (*payload != '/') {
        translate_client_error(client, "malformed PIVOT_ROOT packet");
        return false;
    }

    struct namespace_options *ns = client->ns_options;

    if (ns == nullptr || ns->pivot_root != nullptr) {
        translate_client_error(client,
                               "misplaced PIVOT_ROOT packet");
        return false;
    }

    ns->enable_mount = true;
    ns->pivot_root = payload;
    return true;
}

static bool
translate_client_home(TranslateClient *client,
                      const char *payload)
{
    if (*payload != '/') {
        translate_client_error(client, "malformed HOME packet");
        return false;
    }

    struct namespace_options *ns = client->ns_options;
    struct jail_params *jail = client->jail;

    bool ok = false;

    if (ns != nullptr && ns->home == nullptr) {
        ns->home = payload;
        ok = true;
    }

    if (jail != nullptr && jail->enabled && jail->home_directory == nullptr) {
        jail->home_directory = payload;
        ok = true;
    }

    if (!ok)
        translate_client_error(client,
                               "misplaced HOME packet");

    return ok;
}

static bool
translate_client_mount_proc(TranslateClient *client,
                            size_t payload_length)
{
    if (payload_length > 0) {
        translate_client_error(client, "malformed MOUNT_PROC packet");
        return false;
    }

    struct namespace_options *ns = client->ns_options;

    if (ns == nullptr || ns->mount_proc) {
        translate_client_error(client,
                               "misplaced MOUNT_PROC packet");
        return false;
    }

    ns->enable_mount = true;
    ns->mount_proc = true;
    return true;
}

static bool
translate_client_mount_tmp_tmpfs(TranslateClient *client,
                                 size_t payload_length)
{
    if (payload_length > 0) {
        translate_client_error(client, "malformed MOUNT_TMP_TMPFS packet");
        return false;
    }

    struct namespace_options *ns = client->ns_options;

    if (ns == nullptr || ns->mount_tmp_tmpfs) {
        translate_client_error(client,
                               "misplaced MOUNT_TMP_TMPFS packet");
        return false;
    }

    ns->enable_mount = true;
    ns->mount_tmp_tmpfs = true;
    return true;
}

static bool
translate_client_mount_home(TranslateClient *client,
                            const char *payload)
{
    if (*payload != '/') {
        translate_client_error(client, "malformed MOUNT_HOME packet");
        return false;
    }

    struct namespace_options *ns = client->ns_options;

    if (ns == nullptr || ns->home == nullptr ||
        ns->mount_home != nullptr) {
        translate_client_error(client,
                               "misplaced MOUNT_HOME packet");
        return false;
    }

    ns->enable_mount = true;
    ns->mount_home = payload;
    return true;
}

static bool
translate_client_bind_mount(TranslateClient *client,
                            const char *payload, size_t payload_length)
{
    if (*payload != '/') {
        translate_client_error(client, "malformed BIND_MOUNT packet");
        return false;
    }

    const char *separator = (const char *)memchr(payload, 0, payload_length);
    if (separator == nullptr || separator[1] != '/') {
        translate_client_error(client, "malformed BIND_MOUNT packet");
        return false;
    }

    if (client->mount_list == nullptr) {
        translate_client_error(client,
                               "misplaced BIND_MOUNT packet");
        return false;
    }

    mount_list *m = NewFromPool<mount_list>(client->pool);
    m->next = nullptr;
    m->source = payload + 1; /* skip the slash to make it relative */
    m->target = separator + 1;
    *client->mount_list = m;
    client->mount_list = &m->next;
    return true;
}

static bool
translate_client_uts_namespace(TranslateClient *client,
                               const char *payload)
{
    if (*payload == 0) {
        translate_client_error(client, "malformed MOUNT_UTS_NAMESPACE packet");
        return false;
    }

    struct namespace_options *ns = client->ns_options;

    if (ns == nullptr || ns->hostname != nullptr) {
        translate_client_error(client,
                               "misplaced MOUNT_UTS_NAMESPACE packet");
        return false;
    }

    ns->hostname = payload;
    return true;
}

static bool
translate_client_rlimits(TranslateClient *client,
                         const char *payload)
{
    if (client->child_options == nullptr) {
        translate_client_error(client, "misplaced RLIMITS packet");
        return false;
    }

    if (!rlimit_options_parse(&client->child_options->rlimits, payload)) {
        translate_client_error(client, "malformed RLIMITS packet");
        return false;
    }

    return true;
}

static bool
translate_client_want(TranslateClient *client,
                      const uint16_t *payload, size_t payload_length)
{
    if (client->response.protocol_version < 1) {
        translate_client_error(client, "WANT requires protocol version 1");
        return false;
    }

    if (client->from_request.want) {
        translate_client_error(client, "WANT loop");
        return false;
    }

    if (!client->response.want.IsEmpty()) {
        translate_client_error(client, "duplicate WANT packet");
        return false;
    }

    if (payload_length % sizeof(payload[0]) != 0) {
        translate_client_error(client, "malformed WANT packet");
        return false;
    }

    client->response.want = { payload, payload_length / sizeof(payload[0]) };
    return true;
}

static bool
translate_client_file_not_found(TranslateClient *client,
                                ConstBuffer<void> payload)
{
    if (!client->response.file_not_found.IsNull()) {
        translate_client_error(client, "duplicate FIlE_NOT_FOUND packet");
        return false;
    }

    if (client->response.test_path == nullptr &&
        client->response.expand_test_path == nullptr) {
        switch (client->response.address.type) {
        case RESOURCE_ADDRESS_NONE:
            translate_client_error(client,
                                   "FIlE_NOT_FOUND without resource address");
            return false;

        case RESOURCE_ADDRESS_HTTP:
        case RESOURCE_ADDRESS_AJP:
        case RESOURCE_ADDRESS_PIPE:
            translate_client_error(client,
                                   "FIlE_NOT_FOUND not compatible with resource address");
            return false;

        case RESOURCE_ADDRESS_LOCAL:
        case RESOURCE_ADDRESS_NFS:
        case RESOURCE_ADDRESS_CGI:
        case RESOURCE_ADDRESS_FASTCGI:
        case RESOURCE_ADDRESS_WAS:
        case RESOURCE_ADDRESS_LHTTP:
            break;
        }
    }

    client->response.file_not_found = payload;
    return true;
}

gcc_pure
static bool
has_content_type(const TranslateClient &client)
{
    if (client.file_address != nullptr)
        return client.file_address->content_type != nullptr;
    else if (client.nfs_address != nullptr)
        return client.nfs_address->content_type != nullptr;
    else
        return false;
}

static bool
translate_client_content_type_lookup(TranslateClient &client,
                                     ConstBuffer<void> payload)
{
    if (!client.response.content_type_lookup.IsNull()) {
        translate_client_error(&client, "duplicate CONTENT_TYPE_LOOKUP");
        return false;
    }

    if (has_content_type(client)) {
        translate_client_error(&client, "CONTENT_TYPE/CONTENT_TYPE_LOOKUP conflict");
        return false;
    }

    switch (client.response.address.type) {
    case RESOURCE_ADDRESS_NONE:
        translate_client_error(&client,
                               "CONTENT_TYPE_LOOKUP without resource address");
        return false;

    case RESOURCE_ADDRESS_HTTP:
    case RESOURCE_ADDRESS_LHTTP:
    case RESOURCE_ADDRESS_AJP:
    case RESOURCE_ADDRESS_PIPE:
    case RESOURCE_ADDRESS_CGI:
    case RESOURCE_ADDRESS_FASTCGI:
    case RESOURCE_ADDRESS_WAS:
        translate_client_error(&client,
                               "CONTENT_TYPE_LOOKUP not compatible with resource address");
        return false;

    case RESOURCE_ADDRESS_LOCAL:
    case RESOURCE_ADDRESS_NFS:
        break;
    }

    client.response.content_type_lookup = payload;
    return true;
}

static bool
translate_client_enotdir(TranslateClient &client,
                         ConstBuffer<void> payload)
{
    if (!client.response.enotdir.IsNull()) {
        translate_client_error(&client, "duplicate ENOTDIR");
        return false;
    }

    if (client.response.test_path == nullptr) {
        switch (client.response.address.type) {
        case RESOURCE_ADDRESS_NONE:
            translate_client_error(&client,
                                   "ENOTDIR without resource address");
            return false;

        case RESOURCE_ADDRESS_HTTP:
        case RESOURCE_ADDRESS_AJP:
        case RESOURCE_ADDRESS_PIPE:
        case RESOURCE_ADDRESS_NFS:
            translate_client_error(&client,
                                   "ENOTDIR not compatible with resource address");
            return false;

        case RESOURCE_ADDRESS_LOCAL:
        case RESOURCE_ADDRESS_CGI:
        case RESOURCE_ADDRESS_FASTCGI:
        case RESOURCE_ADDRESS_WAS:
        case RESOURCE_ADDRESS_LHTTP:
            break;
        }
    }

    client.response.enotdir = payload;
    return true;
}

static bool
translate_client_directory_index(TranslateClient &client,
                                     ConstBuffer<void> payload)
{
    if (!client.response.directory_index.IsNull()) {
        translate_client_error(&client, "duplicate DIRECTORY_INDEX");
        return false;
    }

    if (client.response.test_path == nullptr &&
        client.response.expand_test_path == nullptr) {
        switch (client.response.address.type) {
        case RESOURCE_ADDRESS_NONE:
            translate_client_error(&client,
                                   "DIRECTORY_INDEX without resource address");
            return false;

        case RESOURCE_ADDRESS_HTTP:
        case RESOURCE_ADDRESS_LHTTP:
        case RESOURCE_ADDRESS_AJP:
        case RESOURCE_ADDRESS_PIPE:
        case RESOURCE_ADDRESS_CGI:
        case RESOURCE_ADDRESS_FASTCGI:
        case RESOURCE_ADDRESS_WAS:
            translate_client_error(&client,
                                   "DIRECTORY_INDEX not compatible with resource address");
            return false;

        case RESOURCE_ADDRESS_LOCAL:
        case RESOURCE_ADDRESS_NFS:
            break;
        }
    }

    client.response.directory_index = payload;
    return true;
}

static bool
translate_client_expires_relative(TranslateClient &client,
                                  ConstBuffer<void> payload)
{
    if (client.response.expires_relative > 0) {
        translate_client_error(&client, "duplicate EXPIRES_RELATIVE");
        return false;
    }

    if (payload.size != sizeof(uint32_t)) {
        translate_client_error(&client, "malformed EXPIRES_RELATIVE");
        return false;
    }

    client.response.expires_relative = *(const uint32_t *)payload.data;
    return true;
}

static bool
translate_client_stderr_path(TranslateClient &client,
                             ConstBuffer<void> payload)
{
    const char *path = (const char *)payload.data;
    if (*path != '/' || has_null_byte(payload)) {
        translate_client_error(&client, "malformed STDERR_PATH packet");
        return false;
    }

    if (client.child_options == nullptr) {
        translate_client_error(&client, "misplaced STDERR_PATH packet");
        return false;
    }

    if (client.child_options->stderr_path != nullptr) {
        translate_client_error(&client, "duplicate STDERR_PATH packet");
        return false;
    }

    client.child_options->stderr_path = path;
    return true;
}

/**
 * Returns false if the client has been closed.
 */
static bool
translate_handle_packet(TranslateClient *client,
                        enum beng_translation_command command,
                        const void *const _payload,
                        size_t payload_length)
{
    assert(_payload != nullptr);

    const char *const payload = (const char *)_payload;

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

    GError *error = nullptr;

    switch (command) {
    case TRANSLATE_END:
        stopwatch_event(client->stopwatch, "end");

        if (!translate_response_finish(&client->response, &error)) {
            translate_client_abort(client, error);
            return false;
        }

        if (!finish_view(client, &error)) {
            translate_client_abort(client, error);
            return false;
        }

        translate_client_release_socket(client, true);

        async_operation_finished(&client->async);
        client->handler->response(&client->response, client->handler_ctx);
        pool_unref(client->pool);
        return false;

    case TRANSLATE_BEGIN:
        client->response.Clear();
        client->previous_command = command;
        client->resource_address = &client->response.address;
        client->jail = nullptr;
        client->child_options = nullptr;
        client->ns_options = nullptr;
        client->mount_list = nullptr;
        client->file_address = nullptr;
        client->http_address = nullptr;
        client->cgi_address = nullptr;
        client->nfs_address = nullptr;
        client->lhttp_address = nullptr;
        client->address_list = nullptr;

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
        client->response.views = NewFromPool<widget_view>(client->pool);
        widget_view_init(client->response.views);
        client->view = nullptr;
        client->widget_view_tail = &client->response.views->next;
        client->transformation = nullptr;
        client->transformation_tail = &client->response.views->transformation;

        if (payload_length >= sizeof(uint8_t))
            client->response.protocol_version = *(uint8_t *)payload;

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
    case TRANSLATE_SUFFIX:
        translate_client_error(client,
                               "misplaced translate request packet");
        return false;

    case TRANSLATE_STATUS:
        if (payload_length != 2) {
            translate_client_error(client,
                                   "size mismatch in STATUS packet from translation server");
            return false;
        }

        client->response.status = http_status_t(*(const uint16_t*)(const void *)payload);

        if (!http_status_is_valid(client->response.status)) {
            error = g_error_new(translate_quark(), 0,
                                "invalid HTTP status code %u",
                                client->response.status);
            translate_client_abort(client, error);
            return false;
        }

        return true;

    case TRANSLATE_PATH:
        if (client->nfs_address != nullptr && *client->nfs_address->path == 0) {
            if (payload_length == 0 || *payload != '/' ||
                has_null_byte(payload, payload_length)) {
                translate_client_error(client,
                                       "malformed PATH packet");
                return false;
            }

            client->nfs_address->path = payload;
            return true;
        }

        if (client->resource_address == nullptr ||
            client->resource_address->type != RESOURCE_ADDRESS_NONE) {
            translate_client_error(client, "misplaced PATH packet");
            return false;
        }

        if (payload_length == 0 || has_null_byte(payload, payload_length)) {
            translate_client_error(client, "malformed PATH packet");
            return false;
        }

        client->resource_address->type = RESOURCE_ADDRESS_LOCAL;
        client->resource_address->u.file = client->file_address =
            file_address_new(client->pool, payload);
        return true;

    case TRANSLATE_PATH_INFO:
        if (has_null_byte(payload, payload_length)) {
            translate_client_error(client, "malformed PATH_INFO packet");
            return false;
        }

        if (client->cgi_address != nullptr &&
            client->cgi_address->path_info == nullptr) {
            client->cgi_address->path_info = payload;
            return true;
        } else if (client->file_address != nullptr) {
            /* don't emit an error when the resource is a local path.
               This combination might be useful one day, but isn't
               currently used. */
            return true;
        } else {
            translate_client_error(client,
                                   "misplaced PATH_INFO packet");
            return false;
        }

    case TRANSLATE_EXPAND_PATH:
        if (has_null_byte(payload, payload_length)) {
            translate_client_error(client, "malformed EXPAND_PATH packet");
            return false;
        }

        if (client->response.regex == nullptr) {
            translate_client_error(client,
                                   "misplaced EXPAND_PATH packet");
            return false;
        } else if (client->cgi_address != nullptr &&
                   client->cgi_address->expand_path == nullptr) {
            client->cgi_address->expand_path = payload;
            return true;
        } else if (client->nfs_address != nullptr &&
                   client->nfs_address->expand_path == nullptr) {
            client->nfs_address->expand_path = payload;
            return true;
        } else if (client->file_address != nullptr &&
                   client->file_address->expand_path == nullptr) {
            client->file_address->expand_path = payload;
            return true;
        } else if (client->http_address != NULL &&
                   client->http_address->expand_path == NULL) {
            client->http_address->expand_path = payload;
            return true;
        } else {
            translate_client_error(client,
                                   "misplaced EXPAND_PATH packet");
            return false;
        }

    case TRANSLATE_EXPAND_PATH_INFO:
        if (has_null_byte(payload, payload_length)) {
            translate_client_error(client, "malformed EXPAND_PATH_INFO packet");
            return false;
        }

        if (client->response.regex == nullptr) {
            translate_client_error(client,
                                   "misplaced EXPAND_PATH_INFO packet");
            return false;
        } else if (client->cgi_address != nullptr &&
                   client->cgi_address->expand_path_info == nullptr) {
            client->cgi_address->expand_path_info = payload;
            return true;
        } else if (client->file_address != nullptr) {
            /* don't emit an error when the resource is a local path.
               This combination might be useful one day, but isn't
               currently used. */
            return true;
        } else {
            translate_client_error(client,
                                   "misplaced EXPAND_PATH_INFO packet");
            return false;
        }

    case TRANSLATE_DEFLATED:
        if (payload == nullptr || has_null_byte(payload, payload_length)) {
            translate_client_error(client, "malformed DEFLATED packet");
            return false;
        }

        if (client->file_address != nullptr) {
            client->file_address->deflated = payload;
            return true;
        } else if (client->nfs_address != nullptr) {
            /* ignore for now */
        } else {
            translate_client_error(client,
                                   "misplaced DEFLATED packet");
            return false;
        }

    case TRANSLATE_GZIPPED:
        if (payload == nullptr || has_null_byte(payload, payload_length)) {
            translate_client_error(client, "malformed GZIPPED packet");
            return false;
        }

        if (client->file_address != nullptr) {
            client->file_address->gzipped = payload;
            return true;
        } else if (client->nfs_address != nullptr) {
            /* ignore for now */
        } else {
            translate_client_error(client, "misplaced GZIPPED packet");
            return false;
        }

    case TRANSLATE_SITE:
        assert(client->resource_address != nullptr);

        if (payload == nullptr || has_null_byte(payload, payload_length)) {
            translate_client_error(client, "malformed SITE packet");
            return false;
        }

        if (client->resource_address == &client->response.address)
            client->response.site = payload;
        else if (client->jail != nullptr && client->jail->enabled)
            client->jail->site_id = payload;
        else {
            translate_client_error(client, "misplaced SITE packet");
            return false;
        }

        return true;

    case TRANSLATE_CONTENT_TYPE:
        if (payload == nullptr || has_null_byte(payload, payload_length)) {
            translate_client_error(client, "malformed CONTENT_TYPE packet");
            return false;
        }

        if (!client->response.content_type_lookup.IsNull()) {
            translate_client_error(client, "CONTENT_TYPE/CONTENT_TYPE_LOOKUP conflict");
            return false;
        }

        if (client->file_address != nullptr) {
            client->file_address->content_type = payload;
            return true;
        } else if (client->nfs_address != nullptr) {
            client->nfs_address->content_type = payload;
            return true;
        } else if (client->from_request.content_type_lookup) {
            client->response.content_type = payload;
            return true;
        } else {
            translate_client_error(client, "misplaced CONTENT_TYPE packet");
            return false;
        }

    case TRANSLATE_HTTP:
        if (client->resource_address == nullptr ||
            client->resource_address->type != RESOURCE_ADDRESS_NONE) {
            translate_client_error(client, "misplaced HTTP packet");
            return false;
        }

        if (payload_length == 0 || has_null_byte(payload, payload_length)) {
            translate_client_error(client, "malformed HTTP packet");
            return false;
        }

        client->resource_address->type = RESOURCE_ADDRESS_HTTP;
        client->resource_address->u.http = client->http_address =
            http_address_parse(client->pool, payload, &error);
        if (client->http_address == nullptr) {
            translate_client_abort(client, error);
            return false;
        }

        if (client->http_address->scheme != URI_SCHEME_UNIX &&
            client->http_address->scheme != URI_SCHEME_HTTP) {
            translate_client_error(client, "malformed HTTP packet");
            return false;
        }

        client->address_list = &client->http_address->addresses;
        client->default_port = http_address_default_port(client->http_address);
        return true;

    case TRANSLATE_REDIRECT:
        if (payload == nullptr || has_null_byte(payload, payload_length)) {
            translate_client_error(client, "malformed REDIRECT packet");
            return false;
        }

        client->response.redirect = payload;
        return true;

    case TRANSLATE_EXPAND_REDIRECT:
        if (client->response.redirect == nullptr ||
            client->response.expand_redirect != nullptr) {
            translate_client_error(client, "misplaced EXPAND_REDIRECT packet");
            return false;
        }

        if (payload_length == 0 || has_null_byte(payload, payload_length)) {
            translate_client_error(client, "malformed EXPAND_REDIRECT packet");
            return false;
        }

        client->response.expand_redirect = payload;
        return true;

    case TRANSLATE_BOUNCE:
        if (payload == nullptr || has_null_byte(payload, payload_length)) {
            translate_client_error(client, "malformed BOUNCE packet");
            return false;
        }

        client->response.bounce = payload;
        return true;

    case TRANSLATE_FILTER:
        transformation = translate_add_transformation(client);
        transformation->type = transformation::TRANSFORMATION_FILTER;
        transformation->u.filter.type = RESOURCE_ADDRESS_NONE;
        client->resource_address = &transformation->u.filter;
        client->jail = nullptr;
        client->child_options = nullptr;
        client->ns_options = nullptr;
        client->mount_list = nullptr;
        client->file_address = nullptr;
        client->cgi_address = nullptr;
        client->nfs_address = nullptr;
        client->lhttp_address = nullptr;
        client->address_list = nullptr;
        return true;

    case TRANSLATE_FILTER_4XX:
        if (client->view != nullptr)
            client->view->filter_4xx = true;
        else
            client->response.filter_4xx = true;
        return true;

    case TRANSLATE_PROCESS:
        transformation = translate_add_transformation(client);
        transformation->type = transformation::TRANSFORMATION_PROCESS;
        transformation->u.processor.options = PROCESSOR_REWRITE_URL;
        return true;

    case TRANSLATE_DOMAIN:
        daemon_log(2, "deprecated DOMAIN packet\n");
        return true;

    case TRANSLATE_CONTAINER:
        if (client->transformation == nullptr ||
            client->transformation->type != transformation::TRANSFORMATION_PROCESS) {
            translate_client_error(client,
                                   "misplaced CONTAINER packet");
            return false;
        }

        client->transformation->u.processor.options |= PROCESSOR_CONTAINER;
        return true;

    case TRANSLATE_SELF_CONTAINER:
        if (client->transformation == nullptr ||
            client->transformation->type != transformation::TRANSFORMATION_PROCESS) {
            translate_client_error(client,
                                   "misplaced SELF_CONTAINER packet");
            return false;
        }

        client->transformation->u.processor.options |=
            PROCESSOR_SELF_CONTAINER|PROCESSOR_CONTAINER;
        return true;

    case TRANSLATE_GROUP_CONTAINER:
        if (payload_length == 0 || has_null_byte(payload, payload_length)) {
            translate_client_error(client,
                                   "malformed GROUP_CONTAINER packet");
            return false;
        }

        if (client->transformation == nullptr ||
            client->transformation->type != transformation::TRANSFORMATION_PROCESS) {
            translate_client_error(client,
                                   "misplaced GROUP_CONTAINER packet");
            return false;
        }

        client->transformation->u.processor.options |= PROCESSOR_CONTAINER;
        strset_add(client->pool, &client->response.container_groups, payload);
        return true;

    case TRANSLATE_WIDGET_GROUP:
        if (payload_length == 0 || has_null_byte(payload, payload_length)) {
            translate_client_error(client,
                                   "malformed WIDGET_GROUP packet");
            return false;
        }

        client->response.widget_group = payload;
        return true;

    case TRANSLATE_UNTRUSTED:
        if (payload_length == 0 || *payload == '.' ||
            has_null_byte(payload, payload_length) ||
            payload[payload_length - 1] == '.') {
            translate_client_error(client,
                                   "malformed UNTRUSTED packet");
            return false;
        }

        if (client->response.untrusted_prefix != nullptr ||
            client->response.untrusted_site_suffix != nullptr) {
            translate_client_error(client,
                                   "misplaced UNTRUSTED packet");
            return false;
        }

        client->response.untrusted = payload;
        return true;

    case TRANSLATE_UNTRUSTED_PREFIX:
        if (payload_length == 0 || *payload == '.' ||
            has_null_byte(payload, payload_length) ||
            payload[payload_length - 1] == '.') {
            translate_client_error(client,
                                   "malformed UNTRUSTED_PREFIX packet");
            return false;
        }

        if (client->response.untrusted != nullptr ||
            client->response.untrusted_site_suffix != nullptr) {
            translate_client_error(client,
                                   "misplaced UNTRUSTED_PREFIX packet");
            return false;
        }

        client->response.untrusted_prefix = payload;
        return true;

    case TRANSLATE_UNTRUSTED_SITE_SUFFIX:
        if (payload_length == 0 || *payload == '.' ||
            has_null_byte(payload, payload_length) ||
            payload[payload_length - 1] == '.') {
            daemon_log(2, "malformed UNTRUSTED_SITE_SUFFIX packet\n");
            return false;
        }

        if (client->response.untrusted != nullptr ||
            client->response.untrusted_prefix != nullptr) {
            daemon_log(2, "misplaced UNTRUSTED_SITE_SUFFIX packet\n");
            return false;
        }

        client->response.untrusted_site_suffix = payload;
        return true;

    case TRANSLATE_SCHEME:
        if (strncmp(payload, "http", 4) != 0) {
            translate_client_error(client,
                                   "misplaced SCHEME packet");
            return false;
        }

        client->response.scheme = payload;
        return true;

    case TRANSLATE_HOST:
        client->response.host = payload;
        return true;

    case TRANSLATE_URI:
        if (payload_length == 0 || *payload != '/') {
            translate_client_error(client, "malformed URI packet");
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
        client->response.session = { payload, payload_length };
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
        if (client->resource_address == nullptr ||
            client->resource_address->type != RESOURCE_ADDRESS_NONE) {
            translate_client_error(client, "misplaced PIPE packet");
            return false;
        }

        if (payload_length == 0) {
            translate_client_error(client, "malformed PIPE packet");
            return false;
        }

        client->resource_address->type = RESOURCE_ADDRESS_PIPE;
        client->resource_address->u.cgi = client->cgi_address =
            cgi_address_new(client->pool, payload, false);

        client->child_options = &client->cgi_address->options;
        client->ns_options = &client->child_options->ns;
        client->mount_list = &client->ns_options->mounts;
        client->jail = &client->child_options->jail;
        return true;

    case TRANSLATE_CGI:
        if (client->resource_address == nullptr ||
            client->resource_address->type != RESOURCE_ADDRESS_NONE) {
            translate_client_error(client, "misplaced CGI packet");
            return false;
        }

        if (payload_length == 0) {
            translate_client_error(client, "malformed CGI packet");
            return false;
        }

        client->resource_address->type = RESOURCE_ADDRESS_CGI;
        client->resource_address->u.cgi = client->cgi_address =
            cgi_address_new(client->pool, payload, false);

        client->cgi_address->document_root = client->response.document_root;
        client->child_options = &client->cgi_address->options;
        client->ns_options = &client->child_options->ns;
        client->mount_list = &client->ns_options->mounts;
        client->jail = &client->child_options->jail;
        return true;

    case TRANSLATE_FASTCGI:
        if (client->resource_address == nullptr ||
            client->resource_address->type != RESOURCE_ADDRESS_NONE) {
            translate_client_error(client,
                                   "misplaced FASTCGI packet");
            return false;
        }

        if (payload_length == 0) {
            translate_client_error(client,
                                   "malformed FASTCGI packet");
            return false;
        }

        client->resource_address->type = RESOURCE_ADDRESS_FASTCGI;
        client->resource_address->u.cgi = client->cgi_address =
            cgi_address_new(client->pool, payload, true);

        client->child_options = &client->cgi_address->options;
        client->ns_options = &client->child_options->ns;
        client->mount_list = &client->ns_options->mounts;
        client->jail = &client->child_options->jail;
        client->address_list = &client->cgi_address->address_list;
        client->default_port = 9000;
        return true;

    case TRANSLATE_AJP:
        if (client->resource_address == nullptr ||
            client->resource_address->type != RESOURCE_ADDRESS_NONE) {
            translate_client_error(client, "misplaced AJP packet");
            return false;
        }

        if (payload_length == 0) {
            translate_client_error(client, "malformed AJP packet");
            return false;
        }

        client->resource_address->type = RESOURCE_ADDRESS_AJP;
        client->resource_address->u.http = client->http_address =
            http_address_parse(client->pool, payload, &error);
        if (client->http_address == nullptr) {
            translate_client_abort(client, error);
            return false;
        }

        if (client->http_address->scheme != URI_SCHEME_AJP) {
            translate_client_error(client, "malformed AJP packet");
            return false;
        }

        client->address_list = &client->http_address->addresses;
        client->default_port = 8009;
        return true;

    case TRANSLATE_NFS_SERVER:
        if (client->resource_address == nullptr ||
            client->resource_address->type != RESOURCE_ADDRESS_NONE) {
            translate_client_error(client, "misplaced NFS_SERVER packet");
            return false;
        }

        if (payload_length == 0) {
            translate_client_error(client, "malformed NFS_SERVER packet");
            return false;
        }

        client->resource_address->type = RESOURCE_ADDRESS_NFS;
        client->resource_address->u.nfs = client->nfs_address =
            nfs_address_new(client->pool, payload, "", "");
        return true;

    case TRANSLATE_NFS_EXPORT:
        if (client->nfs_address == nullptr ||
            *client->nfs_address->export_name != 0) {
            translate_client_error(client, "misplaced NFS_EXPORT packet");
            return false;
        }

        if (payload_length == 0 || *payload != '/') {
            translate_client_error(client, "malformed NFS_EXPORT packet");
            return false;
        }

        client->nfs_address->export_name = payload;
        return true;

    case TRANSLATE_JAILCGI:
        if (client->jail == nullptr) {
            translate_client_error(client,
                                   "misplaced JAILCGI packet");
            return false;
        }

        client->jail->enabled = true;
        return true;

    case TRANSLATE_HOME:
        return translate_client_home(client, payload);

    case TRANSLATE_INTERPRETER:
        if (client->resource_address == nullptr ||
            (client->resource_address->type != RESOURCE_ADDRESS_CGI &&
             client->resource_address->type != RESOURCE_ADDRESS_FASTCGI) ||
            client->cgi_address->interpreter != nullptr) {
            translate_client_error(client,
                                   "misplaced INTERPRETER packet");
            return false;
        }

        client->cgi_address->interpreter = payload;
        return true;

    case TRANSLATE_ACTION:
        if (client->resource_address == nullptr ||
            (client->resource_address->type != RESOURCE_ADDRESS_CGI &&
             client->resource_address->type != RESOURCE_ADDRESS_FASTCGI) ||
            client->cgi_address->action != nullptr) {
            translate_client_error(client,
                                   "misplaced ACTION packet");
            return false;
        }

        client->cgi_address->action = payload;
        return true;

    case TRANSLATE_SCRIPT_NAME:
        if (client->resource_address == nullptr ||
            (client->resource_address->type != RESOURCE_ADDRESS_CGI &&
             client->resource_address->type != RESOURCE_ADDRESS_WAS &&
             client->resource_address->type != RESOURCE_ADDRESS_FASTCGI) ||
            client->cgi_address->script_name != nullptr) {
            translate_client_error(client,
                                   "misplaced SCRIPT_NAME packet");
            return false;
        }

        client->cgi_address->script_name = payload;
        return true;

    case TRANSLATE_EXPAND_SCRIPT_NAME:
        if (has_null_byte(payload, payload_length)) {
            translate_client_error(client, "malformed EXPAND_SCRIPT_NAME packet");
            return false;
        }

        if (client->response.regex == nullptr ||
            client->cgi_address == nullptr ||
            client->cgi_address->expand_script_name != nullptr) {
            translate_client_error(client,
                                   "misplaced EXPAND_SCRIPT_NAME packet");
            return false;
        }

        client->cgi_address->expand_script_name = payload;
        return true;

    case TRANSLATE_DOCUMENT_ROOT:
        if (*payload != '/') {
            translate_client_error(client, "malformed DOCUMENT_ROOT packet");
            return false;
        }

        if (client->cgi_address != nullptr)
            client->cgi_address->document_root = payload;
        else if (client->file_address != nullptr &&
                 client->file_address->delegate != nullptr)
            client->file_address->document_root = payload;
        else
            client->response.document_root = payload;
        return true;

    case TRANSLATE_ADDRESS:
        if (client->address_list == nullptr) {
            translate_client_error(client,
                                   "misplaced ADDRESS packet");
            return false;
        }

        if (payload_length < 2) {
            translate_client_error(client,
                                   "malformed INTERPRETER packet");
            return false;
        }

        address_list_add(client->pool, client->address_list,
                         (const struct sockaddr *)_payload, payload_length);
        return true;


    case TRANSLATE_ADDRESS_STRING:
        if (client->address_list == nullptr) {
            translate_client_error(client,
                                   "misplaced ADDRESS_STRING packet");
            return false;
        }

        if (payload_length == 0) {
            translate_client_error(client,
                                   "malformed ADDRESS_STRING packet");
            return false;
        }

        {
            bool ret;

            ret = parse_address_string(client->pool, client->address_list,
                                       payload, client->default_port);
            if (!ret) {
                translate_client_error(client,
                                       "malformed ADDRESS_STRING packet");
                return false;
            }
        }

        return true;

    case TRANSLATE_VIEW:
        if (!valid_view_name(payload)) {
            translate_client_error(client, "invalid view name");
            return false;
        }

        if (!add_view(client, payload, &error)) {
            translate_client_abort(client, error);
            return false;
        }

        return true;

    case TRANSLATE_MAX_AGE:
        if (payload_length != 4) {
            translate_client_error(client,
                                   "malformed MAX_AGE packet");
            return false;
        }

        switch (client->previous_command) {
        case TRANSLATE_BEGIN:
            client->response.max_age = *(const uint32_t *)_payload;
            break;

        case TRANSLATE_USER:
            client->response.user_max_age = *(const uint32_t *)_payload;
            break;

        default:
            translate_client_error(client,
                                   "misplaced MAX_AGE packet");
            return false;
        }

        return true;

    case TRANSLATE_VARY:
        if (payload_length == 0 ||
            payload_length % sizeof(client->response.vary.data[0]) != 0) {
            translate_client_error(client, "malformed VARY packet");
            return false;
        }

        client->response.vary.data = (const uint16_t *)_payload;
        client->response.vary.size = payload_length / sizeof(client->response.vary.data[0]);
        return true;

    case TRANSLATE_INVALIDATE:
        if (payload_length == 0 ||
            payload_length % sizeof(client->response.invalidate.data[0]) != 0) {
            translate_client_error(client,
                                   "malformed INVALIDATE packet");
            return false;
        }

        client->response.invalidate.data = (const uint16_t *)_payload;
        client->response.invalidate.size = payload_length /
            sizeof(client->response.invalidate.data[0]);
        return true;

    case TRANSLATE_BASE:
        if (*payload != '/' || payload[payload_length - 1] != '/' ||
            has_null_byte(payload, payload_length)) {
            translate_client_error(client, "malformed BASE packet");
            return false;
        }

        if (client->from_request.uri == nullptr ||
            client->response.auto_base ||
            client->response.base != nullptr) {
            translate_client_error(client, "misplaced BASE packet");
            return false;
        }

        if (memcmp(client->from_request.uri, payload, payload_length) != 0) {
            translate_client_error(client, "BASE mismatches request URI");
            return false;
        }

        client->response.base = payload;
        return true;

    case TRANSLATE_UNSAFE_BASE:
        if (payload_length > 0) {
            translate_client_error(client, "malformed UNSAFE_BASE packet");
            return false;
        }

        if (client->response.base == nullptr) {
            translate_client_error(client, "misplaced UNSAFE_BASE packet");
            return false;
        }

        client->response.unsafe_base = true;
        return true;

    case TRANSLATE_EASY_BASE:
        if (payload_length > 0) {
            translate_client_error(client, "malformed EASY_BASE");
            return false;
        }

        if (client->response.base == nullptr) {
            translate_client_error(client, "EASY_BASE without BASE");
            return false;
        }

        if (client->response.easy_base) {
            translate_client_error(client, "duplicate EASY_BASE");
            return false;
        }

        client->response.easy_base = true;
        return true;

    case TRANSLATE_REGEX:
        if (client->response.base == nullptr) {
            translate_client_error(client, "REGEX without BASE");
            return false;
        }

        if (client->response.regex != nullptr) {
            translate_client_error(client, "duplicate REGEX");
            return false;
        }

        if (payload_length == 0 || has_null_byte(payload, payload_length)) {
            translate_client_error(client, "malformed REGEX packet");
            return false;
        }

        client->response.regex = payload;
        return true;

    case TRANSLATE_INVERSE_REGEX:
        if (client->response.base == nullptr) {
            translate_client_error(client, "INVERSE_REGEX without BASE");
            return false;
        }

        if (client->response.inverse_regex != nullptr) {
            translate_client_error(client, "duplicate INVERSE_REGEX");
            return false;
        }

        if (payload == nullptr || has_null_byte(payload, payload_length)) {
            translate_client_error(client, "malformed INVERSE_REGEX packet");
            return false;
        }

        client->response.inverse_regex = payload;
        return true;

    case TRANSLATE_REGEX_TAIL:
        if (payload_length > 0) {
            translate_client_error(client, "malformed REGEX_TAIL packet");
            return false;
        }

        if (client->response.regex == nullptr &&
            client->response.inverse_regex == nullptr) {
            translate_client_error(client, "misplaced REGEX_TAIL packet");
            return false;
        }

        if (client->response.regex_tail) {
            translate_client_error(client, "duplicate REGEX_TAIL packet");
            return false;
        }

        client->response.regex_tail = true;
        return true;

    case TRANSLATE_REGEX_UNESCAPE:
        if (payload_length > 0) {
            translate_client_error(client, "malformed REGEX_UNESCAPE packet");
            return false;
        }

        if (client->response.regex == nullptr &&
            client->response.inverse_regex == nullptr) {
            translate_client_error(client, "misplaced REGEX_UNESCAPE packet");
            return false;
        }

        if (client->response.regex_unescape) {
            translate_client_error(client, "duplicate REGEX_UNESCAPE packet");
            return false;
        }

        client->response.regex_unescape = true;
        return true;

    case TRANSLATE_DELEGATE:
        if (client->file_address == nullptr) {
            translate_client_error(client,
                                   "misplaced DELEGATE packet");
            return false;
        }

        if (payload_length == 0 || has_null_byte(payload, payload_length)) {
            translate_client_error(client,
                                   "malformed DELEGATE packet");
            return false;
        }

        client->file_address->delegate = payload;
        client->child_options = &client->file_address->child_options;
        client->ns_options = &client->child_options->ns;
        client->mount_list = &client->ns_options->mounts;
        client->jail = &client->child_options->jail;
        return true;

    case TRANSLATE_APPEND:
        if (payload_length == 0 || has_null_byte(payload, payload_length)) {
            translate_client_error(client,
                                   "malformed APPEND packet");
            return false;
        }

        if (client->resource_address == nullptr) {
            translate_client_error(client,
                                   "misplaced APPEND packet");
            return false;
        }

        if (client->cgi_address != nullptr) {
            if (client->cgi_address->args.IsFull()) {
                translate_client_error(client,
                                       "too many APPEND packets");
                return false;
            }

            client->cgi_address->args.Append(payload);
            return true;
        } else if (client->lhttp_address != nullptr) {
            if (client->lhttp_address->args.IsFull()) {
                translate_client_error(client,
                                       "too many APPEND packets");
                return false;
            }

            client->lhttp_address->args.Append(payload);
            return true;
        } else {
            translate_client_error(client,
                                   "misplaced APPEND packet");
            return false;
        }

    case TRANSLATE_EXPAND_APPEND:
        if (payload_length == 0 || has_null_byte(payload, payload_length)) {
            translate_client_error(client,
                                   "malformed EXPAND_APPEND packet");
            return false;
        }

        if (client->resource_address == nullptr) {
            translate_client_error(client,
                                   "misplaced EXPAND_APPEND packet");
            return false;
        }

        if (client->cgi_address != nullptr) {
            if (!client->cgi_address->args.CanSetExpand()) {
                translate_client_error(client,
                                       "misplaced EXPAND_APPEND packet");
                return false;
            }

            client->cgi_address->args.SetExpand(payload);
            return true;
        } else if (client->lhttp_address != nullptr) {
            if (!client->lhttp_address->args.CanSetExpand()) {
                translate_client_error(client,
                                       "misplaced EXPAND_APPEND packet");
                return false;
            }

            client->lhttp_address->args.SetExpand(payload);
            return true;
        } else {
            translate_client_error(client,
                                   "misplaced APPEND packet");
            return false;
        }

    case TRANSLATE_PAIR:
        if (client->cgi_address != nullptr) {
            if (client->cgi_address->env.IsFull()) {
                translate_client_error(client,
                                       "too many PAIR packets");
                return false;
            }

            if (payload_length == 0 || *payload == '=' ||
                has_null_byte(payload, payload_length) ||
                strchr(payload + 1, '=') == nullptr) {
                translate_client_error(client,
                                       "malformed PAIR packet");
                return false;
            }

            client->cgi_address->env.Append(payload);
        } else if (client->lhttp_address != nullptr) {
            if (client->lhttp_address->env.IsFull()) {
                translate_client_error(client,
                                       "too many PAIR packets");
                return false;
            }

            if (payload_length == 0 || *payload == '=' ||
                has_null_byte(payload, payload_length) ||
                strchr(payload + 1, '=') == nullptr) {
                translate_client_error(client,
                                       "malformed PAIR packet");
                return false;
            }

            client->lhttp_address->env.Append(payload);
        } else {
            translate_client_error(client,
                                   "misplaced PAIR packet");
            return false;
        }

        return true;

    case TRANSLATE_EXPAND_PAIR:
        if (client->cgi_address != nullptr) {
            if (!client->cgi_address->env.CanSetExpand()) {
                translate_client_error(client,
                                       "misplaced EXPAND_PAIR packet");
                return false;
            }

            if (payload_length == 0 || *payload == '=' ||
                has_null_byte(payload, payload_length) ||
                strchr(payload + 1, '=') == nullptr) {
                translate_client_error(client,
                                       "malformed EXPAND_PAIR packet");
                return false;
            }

            client->cgi_address->env.SetExpand(payload);
        } else if (client->lhttp_address != nullptr) {
            if (!client->lhttp_address->env.CanSetExpand()) {
                translate_client_error(client,
                                       "misplaced EXPAND_PAIR packet");
                return false;
            }

            if (payload_length == 0 || *payload == '=' ||
                has_null_byte(payload, payload_length) ||
                strchr(payload + 1, '=') == nullptr) {
                translate_client_error(client,
                                       "malformed EXPAND_PAIR packet");
                return false;
            }

            client->lhttp_address->env.SetExpand(payload);
        } else {
            translate_client_error(client,
                                   "misplaced EXPAND_PAIR packet");
            return false;
        }

        return true;

    case TRANSLATE_DISCARD_SESSION:
        client->response.discard_session = true;
        return true;

    case TRANSLATE_REQUEST_HEADER_FORWARD:
        if (client->view != nullptr)
            parse_header_forward(&client->view->request_header_forward,
                                 payload, payload_length);
        else
            parse_header_forward(&client->response.request_header_forward,
                                 payload, payload_length);
        return true;

    case TRANSLATE_RESPONSE_HEADER_FORWARD:
        if (client->view != nullptr)
            parse_header_forward(&client->view->response_header_forward,
                                 payload, payload_length);
        else
            parse_header_forward(&client->response.response_header_forward,
                                 payload, payload_length);
        return true;

    case TRANSLATE_WWW_AUTHENTICATE:
        if (payload_length == 0 || has_null_byte(payload, payload_length)) {
            translate_client_error(client,
                                   "malformed WWW_AUTHENTICATE packet");
            return false;
        }

        client->response.www_authenticate = payload;
        return true;

    case TRANSLATE_AUTHENTICATION_INFO:
        if (payload_length == 0 || has_null_byte(payload, payload_length)) {
            translate_client_error(client,
                                   "malformed AUTHENTICATION_INFO packet");
            return false;
        }

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
        if (client->response.cookie_domain != nullptr) {
            translate_client_error(client,
                                   "misplaced COOKIE_DOMAIN packet");
            return false;
        }

        if (payload_length == 0 || has_null_byte(payload, payload_length)) {
            translate_client_error(client,
                                   "malformed COOKIE_DOMAIN packet");
            return false;
        }

        client->response.cookie_domain = payload;
        return true;

    case TRANSLATE_ERROR_DOCUMENT:
        client->response.error_document = { payload, payload_length };
        return true;

    case TRANSLATE_CHECK:
        if (!client->response.check.IsNull()) {
            translate_client_error(client,
                                   "duplicate CHECK packet");
            return false;
        }

        client->response.check = { payload, payload_length };
        return true;

    case TRANSLATE_PREVIOUS:
        client->response.previous = true;
        return true;

    case TRANSLATE_WAS:
        if (client->resource_address == nullptr ||
            client->resource_address->type != RESOURCE_ADDRESS_NONE) {
            translate_client_error(client,
                                   "misplaced WAS packet");
            return false;
        }

        if (payload_length == 0 || has_null_byte(payload, payload_length)) {
            translate_client_error(client,
                                   "malformed WAS packet");
            return false;
        }

        client->resource_address->type = RESOURCE_ADDRESS_WAS;
        client->resource_address->u.cgi = client->cgi_address =
            cgi_address_new(client->pool, payload, false);

        client->child_options = &client->cgi_address->options;
        client->ns_options = &client->child_options->ns;
        client->mount_list = &client->ns_options->mounts;
        client->jail = &client->child_options->jail;
        return true;

    case TRANSLATE_TRANSPARENT:
        client->response.transparent = true;
        return true;

    case TRANSLATE_WIDGET_INFO:
        client->response.widget_info = true;
        return true;

    case TRANSLATE_STICKY:
        if (client->address_list == nullptr) {
            translate_client_error(client,
                                   "misplaced STICKY packet");
            return false;
        }

        address_list_set_sticky_mode(client->address_list,
                                     STICKY_SESSION_MODULO);
        return true;

    case TRANSLATE_DUMP_HEADERS:
        client->response.dump_headers = true;
        return true;

    case TRANSLATE_COOKIE_HOST:
        if (client->resource_address == nullptr ||
            client->resource_address->type == RESOURCE_ADDRESS_NONE) {
            translate_client_error(client,
                                   "misplaced COOKIE_HOST packet");
            return false;
        }

        if (payload_length == 0 || has_null_byte(payload, payload_length)) {
            translate_client_error(client,
                                   "malformed COOKIE_HOST packet");
            return false;
        }

        client->response.cookie_host = payload;
        return true;

    case TRANSLATE_PROCESS_CSS:
        transformation = translate_add_transformation(client);
        transformation->type = transformation::TRANSFORMATION_PROCESS_CSS;
        transformation->u.css_processor.options = CSS_PROCESSOR_REWRITE_URL;
        return true;

    case TRANSLATE_PREFIX_CSS_CLASS:
        if (client->transformation == nullptr) {
            translate_client_error(client,
                                   "misplaced PREFIX_CSS_CLASS packet");
            return false;
        }

        switch (client->transformation->type) {
        case transformation::TRANSFORMATION_PROCESS:
            client->transformation->u.processor.options |= PROCESSOR_PREFIX_CSS_CLASS;
            break;

        case transformation::TRANSFORMATION_PROCESS_CSS:
            client->transformation->u.css_processor.options |= CSS_PROCESSOR_PREFIX_CLASS;
            break;

        default:
            translate_client_error(client,
                                   "misplaced PREFIX_CSS_CLASS packet");
            return false;
        }

        return true;

    case TRANSLATE_PREFIX_XML_ID:
        if (client->transformation == nullptr) {
            translate_client_error(client,
                                   "misplaced PREFIX_XML_ID packet");
            return false;
        }

        switch (client->transformation->type) {
        case transformation::TRANSFORMATION_PROCESS:
            client->transformation->u.processor.options |= PROCESSOR_PREFIX_XML_ID;
            break;

        case transformation::TRANSFORMATION_PROCESS_CSS:
            client->transformation->u.css_processor.options |= CSS_PROCESSOR_PREFIX_ID;
            break;

        default:
            translate_client_error(client,
                                   "misplaced PREFIX_XML_ID packet");
            return false;
        }

        return true;

    case TRANSLATE_PROCESS_STYLE:
        if (client->transformation == nullptr ||
            client->transformation->type != transformation::TRANSFORMATION_PROCESS) {
            translate_client_error(client,
                                   "misplaced PROCESS_STYLE packet");
            return false;
        }

        client->transformation->u.processor.options |= PROCESSOR_STYLE;
        return true;

    case TRANSLATE_FOCUS_WIDGET:
        if (client->transformation == nullptr ||
            client->transformation->type != transformation::TRANSFORMATION_PROCESS) {
            translate_client_error(client,
                                   "misplaced FOCUS_WIDGET packet");
            return false;
        }

        client->transformation->u.processor.options |= PROCESSOR_FOCUS_WIDGET;
        return true;

    case TRANSLATE_ANCHOR_ABSOLUTE:
        if (client->transformation == nullptr ||
            client->transformation->type != transformation::TRANSFORMATION_PROCESS) {
            translate_client_error(client,
                                   "misplaced ANCHOR_ABSOLUTE packet");
            return false;
        }

        client->response.anchor_absolute = true;
        return true;

    case TRANSLATE_PROCESS_TEXT:
        transformation = translate_add_transformation(client);
        transformation->type = transformation::TRANSFORMATION_PROCESS_TEXT;
        return true;

    case TRANSLATE_LOCAL_URI:
        if (client->response.local_uri != nullptr) {
            translate_client_error(client,
                                   "misplaced LOCAL_URI packet");
            return false;
        }

        if (payload_length == 0 ||
            has_null_byte(payload, payload_length) ||
            payload[payload_length - 1] != '/') {
            translate_client_error(client,
                                   "malformed LOCAL_URI packet");
            return false;
        }

        client->response.local_uri = payload;
        return true;

    case TRANSLATE_AUTO_BASE:
        if (client->resource_address != &client->response.address ||
            client->cgi_address != client->response.address.u.cgi ||
            client->cgi_address->path_info == nullptr ||
            client->from_request.uri == nullptr ||
            client->response.base != nullptr ||
            client->response.auto_base) {
            translate_client_error(client,
                                   "misplaced AUTO_BASE packet");
            return false;
        }

        client->response.auto_base = true;
        return true;

    case TRANSLATE_VALIDATE_MTIME:
        if (payload_length < 10 || payload[8] != '/' ||
            memchr(payload + 9, 0, payload_length - 9) != nullptr) {
            translate_client_error(client,
                                   "malformed VALIDATE_MTIME packet");
            return false;
        }

        client->response.validate_mtime.mtime = *(const uint64_t *)_payload;
        client->response.validate_mtime.path =
            p_strndup(client->pool, payload + 8, payload_length - 8);
        return true;

    case TRANSLATE_LHTTP_PATH:
        if (client->resource_address == nullptr ||
            client->resource_address->type != RESOURCE_ADDRESS_NONE) {
            translate_client_error(client, "misplaced LHTTP_PATH packet");
            return false;
        }

        if (payload_length == 0 || *payload != '/' ||
            has_null_byte(payload, payload_length)) {
            translate_client_error(client, "malformed LHTTP_PATH packet");
            return false;
        }

        client->resource_address->type = RESOURCE_ADDRESS_LHTTP;
        client->resource_address->u.lhttp = client->lhttp_address =
            lhttp_address_new(client->pool, payload);
        client->child_options = &client->lhttp_address->options;
        client->ns_options = &client->child_options->ns;
        client->mount_list = &client->ns_options->mounts;
        client->jail = &client->child_options->jail;
        return true;

    case TRANSLATE_LHTTP_URI:
        if (client->lhttp_address == nullptr ||
            client->lhttp_address->uri != nullptr) {
            translate_client_error(client,
                                   "misplaced LHTTP_HOST packet");
            return false;
        }

        if (payload_length == 0 || *payload != '/' ||
            has_null_byte(payload, payload_length)) {
            translate_client_error(client, "malformed LHTTP_URI packet");
            return false;
        }

        client->lhttp_address->uri = payload;
        return true;

    case TRANSLATE_EXPAND_LHTTP_URI:
        if (client->lhttp_address == nullptr ||
            client->lhttp_address->uri == nullptr ||
            client->lhttp_address->expand_uri != nullptr ||
            client->response.regex == nullptr) {
            translate_client_error(client,
                                   "misplaced EXPAND_LHTTP_URI packet");
            return false;
        }

        if (payload_length == 0 || has_null_byte(payload, payload_length)) {
            translate_client_error(client,
                                   "malformed EXPAND_LHTTP_URI packet");
            return false;
        }

        client->lhttp_address->expand_uri = payload;
        return true;

    case TRANSLATE_LHTTP_HOST:
        if (client->lhttp_address == nullptr ||
            client->lhttp_address->host_and_port != nullptr) {
            translate_client_error(client,
                                   "misplaced LHTTP_HOST packet");
            return false;
        }

        if (payload_length == 0 || has_null_byte(payload, payload_length)) {
            translate_client_error(client, "malformed LHTTP_HOST packet");
            return false;
        }

        client->lhttp_address->host_and_port = payload;
        return true;

    case TRANSLATE_CONCURRENCY:
        if (client->lhttp_address == nullptr) {
            translate_client_error(client,
                                   "misplaced CONCURRENCY packet");
            return false;
        }

        if (payload_length != 2) {
            translate_client_error(client, "malformed CONCURRENCY packet");
            return false;
        }

        client->lhttp_address->concurrency = *(const uint16_t *)_payload;
        return true;

    case TRANSLATE_WANT_FULL_URI:
        if (client->from_request.want_full_uri) {
            translate_client_error(client, "WANT_FULL_URI loop");
            return false;
        }

        if (!client->response.want_full_uri.IsNull()) {
            translate_client_error(client,
                                   "duplicate WANT_FULL_URI packet");
            return false;
        }

        client->response.want_full_uri = { payload, payload_length };
        return true;

    case TRANSLATE_USER_NAMESPACE:
        if (payload_length != 0) {
            translate_client_error(client, "malformed USER_NAMESPACE packet");
            return false;
        }

        if (client->ns_options != nullptr) {
            client->ns_options->enable_user = true;
        } else {
            translate_client_error(client,
                                   "misplaced USER_NAMESPACE packet");
            return false;
        }

        return true;

    case TRANSLATE_PID_NAMESPACE:
        if (payload_length != 0) {
            translate_client_error(client, "malformed PID_NAMESPACE packet");
            return false;
        }

        if (client->ns_options != nullptr) {
            client->ns_options->enable_pid = true;
        } else {
            translate_client_error(client,
                                   "misplaced PID_NAMESPACE packet");
            return false;
        }

        return true;

    case TRANSLATE_NETWORK_NAMESPACE:
        if (payload_length != 0) {
            translate_client_error(client, "malformed NETWORK_NAMESPACE packet");
            return false;
        }

        if (client->ns_options != nullptr) {
            client->ns_options->enable_network = true;
        } else {
            translate_client_error(client,
                                   "misplaced NETWORK_NAMESPACE packet");
            return false;
        }

        return true;

    case TRANSLATE_PIVOT_ROOT:
        return translate_client_pivot_root(client, payload);

    case TRANSLATE_MOUNT_PROC:
        return translate_client_mount_proc(client, payload_length);

    case TRANSLATE_MOUNT_HOME:
        return translate_client_mount_home(client, payload);

    case TRANSLATE_BIND_MOUNT:
        return translate_client_bind_mount(client, payload, payload_length);

    case TRANSLATE_MOUNT_TMP_TMPFS:
        return translate_client_mount_tmp_tmpfs(client, payload_length);

    case TRANSLATE_UTS_NAMESPACE:
        return translate_client_uts_namespace(client, payload);

    case TRANSLATE_RLIMITS:
        return translate_client_rlimits(client, payload);

    case TRANSLATE_WANT:
        return translate_client_want(client, (const uint16_t *)_payload,
                                     payload_length);

    case TRANSLATE_FILE_NOT_FOUND:
        return translate_client_file_not_found(client,
                                               { _payload, payload_length });

    case TRANSLATE_CONTENT_TYPE_LOOKUP:
        return translate_client_content_type_lookup(*client,
                                                    { _payload, payload_length });

    case TRANSLATE_DIRECTORY_INDEX:
        return translate_client_directory_index(*client,
                                                { _payload, payload_length });

    case TRANSLATE_EXPIRES_RELATIVE:
        return translate_client_expires_relative(*client,
                                                 { _payload, payload_length });


    case TRANSLATE_TEST_PATH:
        if (payload_length == 0 || has_null_byte(payload, payload_length) ||
            *payload != '/') {
            translate_client_error(client,
                                   "malformed TEST_PATH packet");
            return false;
        }

        if (client->response.test_path != nullptr) {
            translate_client_error(client,
                                   "duplicate TEST_PATH packet");
            return false;
        }

        client->response.test_path = payload;
        return true;

    case TRANSLATE_EXPAND_TEST_PATH:
        if (payload_length == 0 || has_null_byte(payload, payload_length)) {
            translate_client_error(client,
                                   "malformed EXPAND_TEST_PATH packet");
            return false;
        }

        if (client->response.expand_test_path != nullptr) {
            translate_client_error(client,
                                   "duplicate EXPAND_TEST_PATH packet");
            return false;
        }

        client->response.expand_test_path = payload;
        return true;

    case TRANSLATE_REDIRECT_QUERY_STRING:
        if (payload_length != 0) {
            translate_client_error(client, "malformed REDIRECT_QUERY_STRING packet");
            return false;
        }

        if (client->response.redirect_query_string ||
            (client->response.redirect == nullptr &&
             client->response.expand_redirect == nullptr)) {
            translate_client_error(client, "misplaced REDIRECT_QUERY_STRING packet");
            return false;
        }

        client->response.redirect_query_string = true;
        return true;

    case TRANSLATE_ENOTDIR:
        return translate_client_enotdir(*client, { _payload, payload_length });

    case TRANSLATE_STDERR_PATH:
        return translate_client_stderr_path(*client,
                                            { _payload, payload_length });
    }

    error = g_error_new(translate_quark(), 0,
                        "unknown translation packet: %u", command);
    translate_client_abort(client, error);
    return false;
}

static BufferedResult
translate_client_feed(TranslateClient *client,
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
        client->socket.Consumed(nbytes);

        if (client->reader.state != TranslatePacketReader::State::COMPLETE)
            /* need more data */
            break;

        if (!translate_handle_packet(client,
                                     (enum beng_translation_command)client->reader.header.command,
                                     client->reader.payload == nullptr
                                     ? "" : client->reader.payload,
                                     client->reader.header.length))
            return BufferedResult::CLOSED;
    }

    return BufferedResult::MORE;
}

/*
 * send requests
 *
 */

static bool
translate_try_write(TranslateClient *client)
{
    size_t length;
    const void *data = growing_buffer_reader_read(&client->request, &length);
    assert(data != nullptr);

    ssize_t nbytes = client->socket.Write(data, length);
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

        client->socket.UnscheduleWrite();

        packet_reader_init(&client->reader);
        return client->socket.Read(true);
    }

    client->socket.ScheduleWrite();
    return true;
}


/*
 * buffered_socket handler
 *
 */

static BufferedResult
translate_client_socket_data(const void *buffer, size_t size, void *ctx)
{
    TranslateClient *client = (TranslateClient *)ctx;

    return translate_client_feed(client, (const uint8_t *)buffer, size);
}

static bool
translate_client_socket_closed(void *ctx)
{
    TranslateClient *client = (TranslateClient *)ctx;

    translate_client_release_socket(client, false);
    return true;
}

static bool
translate_client_socket_write(void *ctx)
{
    TranslateClient *client = (TranslateClient *)ctx;

    return translate_try_write(client);
}

static void
translate_client_socket_error(GError *error, void *ctx)
{
    TranslateClient *client = (TranslateClient *)ctx;

    g_prefix_error(&error, "Translation server connection failed: ");
    translate_client_abort(client, error);
}

static constexpr BufferedSocketHandler translate_client_socket_handler = {
    .data = translate_client_socket_data,
    .closed = translate_client_socket_closed,
    .write = translate_client_socket_write,
    .error = translate_client_socket_error,
};

/*
 * async operation
 *
 */

static void
translate_connection_abort(struct async_operation *ao)
{
    TranslateClient *client = TranslateClient::FromAsync(ao);

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
          const TranslateRequest *request,
          const TranslateHandler *handler, void *ctx,
          struct async_operation_ref *async_ref)
{
    assert(pool != nullptr);
    assert(fd >= 0);
    assert(lease != nullptr);
    assert(request != nullptr);
    assert(request->uri != nullptr || request->widget_type != nullptr ||
           (!request->content_type_lookup.IsNull() &&
            request->suffix != nullptr));
    assert(handler != nullptr);
    assert(handler->response != nullptr);
    assert(handler->error != nullptr);

    GError *error = nullptr;
    struct growing_buffer *gb = marshal_request(pool, request, &error);
    if (gb == nullptr) {
        lease_direct_release(lease, lease_ctx, true);

        handler->error(error, ctx);
        pool_unref(pool);
        return;
    }

    TranslateClient *client = NewFromPool<TranslateClient>(pool);
    client->pool = pool;
    client->stopwatch = stopwatch_fd_new(pool, fd,
                                         request->uri != nullptr ? request->uri
                                         : request->widget_type);
    client->socket.Init(pool, fd, ISTREAM_SOCKET,
                        &translate_read_timeout,
                        &translate_write_timeout,
                        &translate_client_socket_handler, client);
    p_lease_ref_set(&client->lease_ref, lease, lease_ctx,
                    pool, "translate_lease");

    client->from_request.uri = request->uri;
    client->from_request.want_full_uri = !request->want_full_uri.IsNull();
    client->from_request.want = !request->want.IsEmpty();
    client->from_request.content_type_lookup =
        !request->content_type_lookup.IsNull();

    growing_buffer_reader_init(&client->request, gb);
    client->handler = handler;
    client->handler_ctx = ctx;
    client->response.status = (http_status_t)-1;

    async_init(&client->async, &translate_operation);
    async_ref_set(async_ref, &client->async);

    pool_ref(client->pool);
    translate_try_write(client);
}
