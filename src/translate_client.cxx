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
#include "please.hxx"
#include "growing_buffer.hxx"
#include "processor.h"
#include "css_processor.h"
#include "async.hxx"
#include "file_address.hxx"
#include "lhttp_address.hxx"
#include "http_address.hxx"
#include "cgi_address.hxx"
#include "nfs_address.hxx"
#include "mount_list.hxx"
#include "strutil.h"
#include "strmap.hxx"
#include "stopwatch.h"
#include "beng-proxy/translation.h"
#include "gerrno.h"
#include "pool.hxx"
#include "net/SocketAddress.hxx"
#include "util/Cast.hxx"
#include "util/CharUtil.hxx"

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
    GrowingBufferReader request;

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
    AddressList *address_list;

    /**
     * Default port for #TRANSLATE_ADDRESS_STRING.
     */
    int default_port;

    /** the current widget view */
    WidgetView *view;

    /** pointer to the tail of the transformation view linked list */
    WidgetView **widget_view_tail;

    /** the current transformation */
    Transformation *transformation;

    /** pointer to the tail of the transformation linked list */
    Transformation **transformation_tail;

    /** this asynchronous operation is the translate request; aborting
        it causes the request to be cancelled */
    struct async_operation async;

    static TranslateClient *FromAsync(async_operation *ao) {
        return &ContainerCast2(*ao, &TranslateClient::async);
    }

    explicit TranslateClient(const GrowingBuffer &_request)
        :request(_request) {}

    void ReleaseSocket(bool reuse);
    void Release(bool reuse);

    void Fail(GError *error);

    void Fail(const char *msg) {
        Fail(g_error_new_literal(translate_quark(), 0, msg));
    }

    template<typename... Args>
    void Fail(const char *fmt, Args&&... args) {
        static_assert(sizeof...(Args) > 0, "empty argument list");

        Fail(g_error_new(translate_quark(), 0, fmt,
                         std::forward<Args>(args)...));
    }

    bool AddView(const char *name, GError **error_r);

    /**
     * Returns false if the client has been closed.
     */
    bool HandlePacket(enum beng_translation_command command,
                      const void *const _payload, size_t payload_length);

    BufferedResult Feed(const uint8_t *data, size_t length);
};

static const struct timeval translate_read_timeout = {
    .tv_sec = 60,
    .tv_usec = 0,
};

static const struct timeval translate_write_timeout = {
    .tv_sec = 10,
    .tv_usec = 0,
};

void
TranslateClient::ReleaseSocket(bool reuse)
{
    assert(socket.IsConnected());

    stopwatch_dump(stopwatch);

    socket.Abandon();
    socket.Destroy();

    p_lease_release(lease_ref, reuse, *pool);
}

/**
 * Release resources held by this object: the event object, the socket
 * lease, and the pool reference.
 */
void
TranslateClient::Release(bool reuse)
{
    ReleaseSocket(reuse);
    pool_unref(pool);
}

void
TranslateClient::Fail(GError *error)
{
    stopwatch_event(stopwatch, "error");

    ReleaseSocket(false);

    async.Finished();
    handler->error(error, handler_ctx);
    pool_unref(pool);
}


/*
 * request marshalling
 *
 */

static bool
write_packet_n(GrowingBuffer *gb, uint16_t command,
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
write_packet(GrowingBuffer *gb, uint16_t command,
             const char *payload, GError **error_r)
{
    return write_packet_n(gb, command, payload,
                          payload != nullptr ? strlen(payload) : 0,
                          error_r);
}

template<typename T>
static bool
write_buffer(GrowingBuffer *gb, uint16_t command,
             ConstBuffer<T> buffer, GError **error_r)
{
    auto b = buffer.ToVoid();
    return write_packet_n(gb, command, b.data, b.size, error_r);
}

/**
 * Forward the command to write_packet() only if #payload is not nullptr.
 */
static bool
write_optional_packet(GrowingBuffer *gb, uint16_t command,
                      const char *payload, GError **error_r)
{
    if (payload == nullptr)
        return true;

    return write_packet(gb, command, payload, error_r);
}

template<typename T>
static bool
write_optional_buffer(GrowingBuffer *gb, uint16_t command,
                      ConstBuffer<T> buffer, GError **error_r)
{
    return buffer.IsNull() || write_buffer(gb, command, buffer, error_r);
}

static bool
write_short(GrowingBuffer *gb,
            uint16_t command, uint16_t payload, GError **error_r)
{
    return write_packet_n(gb, command, &payload, sizeof(payload), error_r);
}

static bool
write_sockaddr(GrowingBuffer *gb,
               uint16_t command, uint16_t command_string,
               SocketAddress address,
               GError **error_r)
{
    assert(!address.IsNull());

    char address_string[1024];
    return write_packet_n(gb, command,
                          address.GetAddress(), address.GetSize(),
                          error_r) &&
        (!socket_address_to_string(address_string, sizeof(address_string),
                                   address.GetAddress(), address.GetSize()) ||
         write_packet(gb, command_string, address_string, error_r));
}

static bool
write_optional_sockaddr(GrowingBuffer *gb,
                        uint16_t command, uint16_t command_string,
                        SocketAddress address,
                        GError **error_r)
{
    return !address.IsNull()
        ? write_sockaddr(gb, command, command_string, address,
                         error_r)
        : true;
}

static GrowingBuffer *
marshal_request(struct pool *pool, const TranslateRequest *request,
                GError **error_r)
{
    GrowingBuffer *gb;
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
        write_optional_packet(gb, TRANSLATE_LISTENER_TAG,
                              request->listener_tag, error_r) &&
        write_optional_sockaddr(gb, TRANSLATE_LOCAL_ADDRESS,
                                TRANSLATE_LOCAL_ADDRESS_STRING,
                                request->local_address, error_r) &&
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
        write_optional_buffer(gb, TRANSLATE_AUTH, request->auth, error_r) &&
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
        write_optional_buffer(gb, TRANSLATE_PROBE_PATH_SUFFIXES,
                              request->probe_path_suffixes,
                              error_r) &&
        write_optional_packet(gb, TRANSLATE_PROBE_SUFFIX,
                              request->probe_suffix,
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
        reader->payload = PoolAlloc<char>(*pool, reader->header.length + 1);
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
is_valid_nonempty_string(const char *p, size_t size)
{
    return size > 0 && !has_null_byte(p, size);
}

gcc_pure
static bool
is_valid_absolute_path(const char *p, size_t size)
{
    return is_valid_nonempty_string(p, size) && *p == '/';
}

gcc_pure
static bool
is_valid_absolute_uri(const char *p, size_t size)
{
    return is_valid_absolute_path(p, size);
}

static Transformation *
translate_add_transformation(TranslateClient *client)
{
    auto transformation = NewFromPool<Transformation>(*client->pool);

    transformation->next = nullptr;
    client->transformation = transformation;
    *client->transformation_tail = transformation;
    client->transformation_tail = &transformation->next;

    return transformation;
}

static bool
parse_address_string(struct pool *pool, AddressList *list,
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

        socklen_t size = SUN_LEN(&sun);

        if (*p == '@')
            /* abstract socket */
            sun.sun_path[0] = 0;

        list->Add(pool, { (const struct sockaddr *)&sun, size });
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
        list->Add(pool, {i->ai_addr, i->ai_addrlen});

    freeaddrinfo(ai);
    return true;
}

static bool
valid_view_name_char(char ch)
{
    return IsAlphaNumericASCII(ch) || ch == '_' || ch == '-';
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

/**
 * Finish the settings in the current view, i.e. copy attributes from
 * the "parent" view.
 */
static bool
finish_view(TranslateClient *client, GError **error_r)
{
    assert(client != nullptr);
    assert(client->response.views != nullptr);

    WidgetView *view = client->view;
    if (client->view == nullptr) {
        view = client->response.views;
        assert(view != nullptr);

        const struct resource_address *address = &client->response.address;
        if (address->type != RESOURCE_ADDRESS_NONE &&
            view->address.type == RESOURCE_ADDRESS_NONE) {
            /* no address yet: copy address from response */
            resource_address_copy(*client->pool, &view->address, address);
            view->filter_4xx = client->response.filter_4xx;
        }

        view->request_header_forward = client->response.request_header_forward;
        view->response_header_forward = client->response.response_header_forward;
    } else {
        if (client->view->address.type == RESOURCE_ADDRESS_NONE &&
            client->view != client->response.views)
            /* no address yet: inherits settings from the default view */
            client->view->InheritFrom(*client->pool,
                                      *client->response.views);
    }

    if (!view->address.Check(error_r))
        return false;

    return true;
}

inline bool
TranslateClient::AddView(const char *name, GError **error_r)
{
    if (!finish_view(this, error_r))
        return false;

    auto new_view = NewFromPool<WidgetView>(*pool);
    new_view->Init(name);
    new_view->request_header_forward = response.request_header_forward;
    new_view->response_header_forward = response.response_header_forward;

    view = new_view;
    *widget_view_tail = new_view;
    widget_view_tail = &new_view->next;
    resource_address = &new_view->address;
    jail = nullptr;
    child_options = nullptr;
    ns_options = nullptr;
    mount_list = nullptr;
    file_address = nullptr;
    http_address = nullptr;
    cgi_address = nullptr;
    nfs_address = nullptr;
    lhttp_address = nullptr;
    address_list = nullptr;
    transformation_tail = &new_view->transformation;
    transformation = nullptr;

    return true;
}

static bool
parse_header_forward(TranslateClient &client,
                     struct header_forward_settings *settings,
                     const void *payload, size_t payload_length)
{
    const beng_header_forward_packet *packet =
        (const beng_header_forward_packet *)payload;

    if (payload_length % sizeof(*packet) != 0) {
        client.Fail("malformed header forward packet");
        return false;
    }

    while (payload_length > 0) {
        if (packet->group < HEADER_GROUP_ALL ||
            packet->group >= HEADER_GROUP_MAX ||
            (packet->mode != HEADER_FORWARD_NO &&
             packet->mode != HEADER_FORWARD_YES &&
             packet->mode != HEADER_FORWARD_BOTH &&
             packet->mode != HEADER_FORWARD_MANGLE) ||
            packet->reserved != 0) {
            client.Fail("malformed header forward packet");
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
parse_header(struct pool *pool,
             KeyValueList &headers, const char *packet_name,
             const char *payload, size_t payload_length,
             GError **error_r)
{
    const char *value = (const char *)memchr(payload, ':', payload_length);
    if (value == nullptr || value == payload ||
        has_null_byte(payload, payload_length)) {
        g_set_error(error_r, translate_quark(), 0, "malformed %s packet",
                    packet_name);
        return false;
    }

    char *name = p_strndup(pool, payload, value - payload);
    ++value;

    str_to_lower(name);

    if (!http_header_name_valid(name)) {
        g_set_error(error_r, translate_quark(), 0,
                    "malformed name in %s packet", packet_name);
        return false;
    } else if (http_header_is_hop_by_hop(name)) {
        g_set_error(error_r, translate_quark(), 0, "hop-by-hop %s packet",
                    packet_name);
        return false;
    }

    headers.Add(PoolAllocator(*pool), name, value);
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
    if (!response->address.Check(error_r))
        return false;

    if (response->easy_base && !response->address.IsValidBase()) {
        /* EASY_BASE was enabled, but the resource address does not
           end with a slash, thus LoadBase() cannot work */
        g_set_error(error_r, translate_quark(), 0, "Invalid base address");
        return nullptr;
    }

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

    /* these lists are in reverse order because new items were added
       to the front; reverse them now */
    response->request_headers.Reverse();
    response->response_headers.Reverse();

    if (!response->probe_path_suffixes.IsNull() &&
        response->probe_suffixes.empty()) {
        g_set_error(error_r, translate_quark(), 0,
                    "PROBE_PATH_SUFFIX without PROBE_SUFFIX");
        return nullptr;
    }

    return true;
}

gcc_pure
static bool
translate_client_check_pair(const char *payload, size_t payload_length)
{
    return payload_length > 0 && *payload != '=' &&
        !has_null_byte(payload, payload_length) &&
        strchr(payload + 1, '=') != nullptr;
}

static bool
translate_client_check_pair(TranslateClient &client, const char *name,
                            const char *payload, size_t payload_length)
{
    if (!translate_client_check_pair(payload, payload_length)) {
        client.Fail("malformed %s packet", name);
        return false;
    }

    return true;
}

static bool
translate_client_pair(TranslateClient *client,
                      struct param_array &array, const char *name,
                      const char *payload, size_t payload_length)
{
    if (array.IsFull()) {
        client->Fail("too many %s packets", name);
        return false;
    }

    if (!translate_client_check_pair(*client, name,
                                     payload, payload_length))
        return false;

    array.Append(payload);
    return true;
}

static bool
translate_client_expand_pair(TranslateClient *client,
                             struct param_array &array, const char *name,
                             const char *payload, size_t payload_length)
{
    if (!array.CanSetExpand()) {
        client->Fail("misplaced %s packet", name);
        return false;
    }

    if (!translate_client_check_pair(*client, name,
                                     payload, payload_length))
        return false;

    array.SetExpand(payload);
    return true;
}

static bool
translate_client_pivot_root(TranslateClient *client,
                            const char *payload, size_t payload_length)
{
    if (!is_valid_absolute_path(payload, payload_length)) {
        client->Fail("malformed PIVOT_ROOT packet");
        return false;
    }

    struct namespace_options *ns = client->ns_options;

    if (ns == nullptr || ns->pivot_root != nullptr) {
        client->Fail("misplaced PIVOT_ROOT packet");
        return false;
    }

    ns->enable_mount = true;
    ns->pivot_root = payload;
    return true;
}

static bool
translate_client_home(TranslateClient *client,
                      const char *payload, size_t payload_length)
{
    if (!is_valid_absolute_path(payload, payload_length)) {
        client->Fail("malformed HOME packet");
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
        client->Fail("misplaced HOME packet");

    return ok;
}

static bool
translate_client_mount_proc(TranslateClient *client,
                            size_t payload_length)
{
    if (payload_length > 0) {
        client->Fail("malformed MOUNT_PROC packet");
        return false;
    }

    struct namespace_options *ns = client->ns_options;

    if (ns == nullptr || ns->mount_proc) {
        client->Fail("misplaced MOUNT_PROC packet");
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
        client->Fail("malformed MOUNT_TMP_TMPFS packet");
        return false;
    }

    struct namespace_options *ns = client->ns_options;

    if (ns == nullptr || ns->mount_tmp_tmpfs) {
        client->Fail("misplaced MOUNT_TMP_TMPFS packet");
        return false;
    }

    ns->enable_mount = true;
    ns->mount_tmp_tmpfs = true;
    return true;
}

static bool
translate_client_mount_home(TranslateClient *client,
                            const char *payload, size_t payload_length)
{
    if (!is_valid_absolute_path(payload, payload_length)) {
        client->Fail("malformed MOUNT_HOME packet");
        return false;
    }

    struct namespace_options *ns = client->ns_options;

    if (ns == nullptr || ns->home == nullptr ||
        ns->mount_home != nullptr) {
        client->Fail("misplaced MOUNT_HOME packet");
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
        client->Fail("malformed BIND_MOUNT packet");
        return false;
    }

    const char *separator = (const char *)memchr(payload, 0, payload_length);
    if (separator == nullptr || separator[1] != '/') {
        client->Fail("malformed BIND_MOUNT packet");
        return false;
    }

    if (client->mount_list == nullptr) {
        client->Fail("misplaced BIND_MOUNT packet");
        return false;
    }

    mount_list *m = NewFromPool<mount_list>(*client->pool);
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
        client->Fail("malformed MOUNT_UTS_NAMESPACE packet");
        return false;
    }

    struct namespace_options *ns = client->ns_options;

    if (ns == nullptr || ns->hostname != nullptr) {
        client->Fail("misplaced MOUNT_UTS_NAMESPACE packet");
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
        client->Fail("misplaced RLIMITS packet");
        return false;
    }

    if (!rlimit_options_parse(&client->child_options->rlimits, payload)) {
        client->Fail("malformed RLIMITS packet");
        return false;
    }

    return true;
}

static bool
translate_client_want(TranslateClient *client,
                      const uint16_t *payload, size_t payload_length)
{
    if (client->response.protocol_version < 1) {
        client->Fail("WANT requires protocol version 1");
        return false;
    }

    if (client->from_request.want) {
        client->Fail("WANT loop");
        return false;
    }

    if (!client->response.want.IsEmpty()) {
        client->Fail("duplicate WANT packet");
        return false;
    }

    if (payload_length % sizeof(payload[0]) != 0) {
        client->Fail("malformed WANT packet");
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
        client->Fail("duplicate FIlE_NOT_FOUND packet");
        return false;
    }

    if (client->response.test_path == nullptr &&
        client->response.expand_test_path == nullptr) {
        switch (client->response.address.type) {
        case RESOURCE_ADDRESS_NONE:
            client->Fail("FIlE_NOT_FOUND without resource address");
            return false;

        case RESOURCE_ADDRESS_HTTP:
        case RESOURCE_ADDRESS_AJP:
        case RESOURCE_ADDRESS_PIPE:
            client->Fail("FIlE_NOT_FOUND not compatible with resource address");
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
        client.Fail("duplicate CONTENT_TYPE_LOOKUP");
        return false;
    }

    if (has_content_type(client)) {
        client.Fail("CONTENT_TYPE/CONTENT_TYPE_LOOKUP conflict");
        return false;
    }

    switch (client.response.address.type) {
    case RESOURCE_ADDRESS_NONE:
        client.Fail("CONTENT_TYPE_LOOKUP without resource address");
        return false;

    case RESOURCE_ADDRESS_HTTP:
    case RESOURCE_ADDRESS_LHTTP:
    case RESOURCE_ADDRESS_AJP:
    case RESOURCE_ADDRESS_PIPE:
    case RESOURCE_ADDRESS_CGI:
    case RESOURCE_ADDRESS_FASTCGI:
    case RESOURCE_ADDRESS_WAS:
        client.Fail("CONTENT_TYPE_LOOKUP not compatible with resource address");
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
        client.Fail("duplicate ENOTDIR");
        return false;
    }

    if (client.response.test_path == nullptr) {
        switch (client.response.address.type) {
        case RESOURCE_ADDRESS_NONE:
            client.Fail("ENOTDIR without resource address");
            return false;

        case RESOURCE_ADDRESS_HTTP:
        case RESOURCE_ADDRESS_AJP:
        case RESOURCE_ADDRESS_PIPE:
        case RESOURCE_ADDRESS_NFS:
            client.Fail("ENOTDIR not compatible with resource address");
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
        client.Fail("duplicate DIRECTORY_INDEX");
        return false;
    }

    if (client.response.test_path == nullptr &&
        client.response.expand_test_path == nullptr) {
        switch (client.response.address.type) {
        case RESOURCE_ADDRESS_NONE:
            client.Fail("DIRECTORY_INDEX without resource address");
            return false;

        case RESOURCE_ADDRESS_HTTP:
        case RESOURCE_ADDRESS_LHTTP:
        case RESOURCE_ADDRESS_AJP:
        case RESOURCE_ADDRESS_PIPE:
        case RESOURCE_ADDRESS_CGI:
        case RESOURCE_ADDRESS_FASTCGI:
        case RESOURCE_ADDRESS_WAS:
            client.Fail("DIRECTORY_INDEX not compatible with resource address");
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
        client.Fail("duplicate EXPIRES_RELATIVE");
        return false;
    }

    if (payload.size != sizeof(uint32_t)) {
        client.Fail("malformed EXPIRES_RELATIVE");
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
    if (!is_valid_absolute_path(path, payload.size)) {
        client.Fail("malformed STDERR_PATH packet");
        return false;
    }

    if (client.child_options == nullptr) {
        client.Fail("misplaced STDERR_PATH packet");
        return false;
    }

    if (client.child_options->stderr_path != nullptr) {
        client.Fail("duplicate STDERR_PATH packet");
        return false;
    }

    client.child_options->stderr_path = path;
    return true;
}

static bool
CheckProbeSuffix(const char *payload, size_t length)
{
    return memchr(payload, '/', length) == nullptr &&
        !has_null_byte(payload, length);
}

inline bool
TranslateClient::HandlePacket(enum beng_translation_command command,
                              const void *const _payload,
                              size_t payload_length)
{
    assert(_payload != nullptr);

    const char *const payload = (const char *)_payload;

    if (command == TRANSLATE_BEGIN) {
        if (response.status != (http_status_t)-1) {
            GError *error =
                g_error_new_literal(translate_quark(), 0,
                                    "double BEGIN from translation server");
            Fail(error);
            return false;
        }
    } else {
        if (response.status == (http_status_t)-1) {
            GError *error =
                g_error_new_literal(translate_quark(), 0,
                                    "no BEGIN from translation server");
            Fail(error);
            return false;
        }
    }

    GError *error = nullptr;

    switch (command) {
        Transformation *new_transformation;

    case TRANSLATE_END:
        stopwatch_event(stopwatch, "end");

        if (!translate_response_finish(&response, &error)) {
            Fail(error);
            return false;
        }

        if (!finish_view(this, &error)) {
            Fail(error);
            return false;
        }

        ReleaseSocket(true);

        async.Finished();
        handler->response(&response, handler_ctx);
        pool_unref(pool);
        return false;

    case TRANSLATE_BEGIN:
        response.Clear();
        previous_command = command;
        resource_address = &response.address;
        jail = nullptr;
        child_options = nullptr;
        ns_options = nullptr;
        mount_list = nullptr;
        file_address = nullptr;
        http_address = nullptr;
        cgi_address = nullptr;
        nfs_address = nullptr;
        lhttp_address = nullptr;
        address_list = nullptr;

        response.views = NewFromPool<WidgetView>(*pool);
        response.views->Init(nullptr);
        view = nullptr;
        widget_view_tail = &response.views->next;
        transformation = nullptr;
        transformation_tail = &response.views->transformation;

        if (payload_length >= sizeof(uint8_t))
            response.protocol_version = *(uint8_t *)payload;

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
    case TRANSLATE_LISTENER_TAG:
        Fail("misplaced translate request packet");
        return false;

    case TRANSLATE_STATUS:
        if (payload_length != 2) {
            Fail("size mismatch in STATUS packet from translation server");
            return false;
        }

        response.status = http_status_t(*(const uint16_t*)(const void *)payload);

        if (!http_status_is_valid(response.status)) {
            Fail("invalid HTTP status code %u", response.status);
            return false;
        }

        return true;

    case TRANSLATE_PATH:
        if (!is_valid_absolute_path(payload, payload_length)) {
            Fail("malformed PATH packet");
            return false;
        }

        if (nfs_address != nullptr && *nfs_address->path == 0) {
            nfs_address->path = payload;
            return true;
        }

        if (resource_address == nullptr ||
            resource_address->type != RESOURCE_ADDRESS_NONE) {
            Fail("misplaced PATH packet");
            return false;
        }

        resource_address->type = RESOURCE_ADDRESS_LOCAL;
        resource_address->u.file = file_address =
            file_address_new(*pool, payload);
        return true;

    case TRANSLATE_PATH_INFO:
        if (has_null_byte(payload, payload_length)) {
            Fail("malformed PATH_INFO packet");
            return false;
        }

        if (cgi_address != nullptr &&
            cgi_address->path_info == nullptr) {
            cgi_address->path_info = payload;
            return true;
        } else if (file_address != nullptr) {
            /* don't emit an error when the resource is a local path.
               This combination might be useful one day, but isn't
               currently used. */
            return true;
        } else {
            Fail("misplaced PATH_INFO packet");
            return false;
        }

    case TRANSLATE_EXPAND_PATH:
        if (has_null_byte(payload, payload_length)) {
            Fail("malformed EXPAND_PATH packet");
            return false;
        }

        if (response.regex == nullptr) {
            Fail("misplaced EXPAND_PATH packet");
            return false;
        } else if (cgi_address != nullptr &&
                   cgi_address->expand_path == nullptr) {
            cgi_address->expand_path = payload;
            return true;
        } else if (nfs_address != nullptr &&
                   nfs_address->expand_path == nullptr) {
            nfs_address->expand_path = payload;
            return true;
        } else if (file_address != nullptr &&
                   file_address->expand_path == nullptr) {
            file_address->expand_path = payload;
            return true;
        } else if (http_address != NULL &&
                   http_address->expand_path == NULL) {
            http_address->expand_path = payload;
            return true;
        } else {
            Fail("misplaced EXPAND_PATH packet");
            return false;
        }

    case TRANSLATE_EXPAND_PATH_INFO:
        if (has_null_byte(payload, payload_length)) {
            Fail("malformed EXPAND_PATH_INFO packet");
            return false;
        }

        if (response.regex == nullptr) {
            Fail("misplaced EXPAND_PATH_INFO packet");
            return false;
        } else if (cgi_address != nullptr &&
                   cgi_address->expand_path_info == nullptr) {
            cgi_address->expand_path_info = payload;
            return true;
        } else if (file_address != nullptr) {
            /* don't emit an error when the resource is a local path.
               This combination might be useful one day, but isn't
               currently used. */
            return true;
        } else {
            Fail("misplaced EXPAND_PATH_INFO packet");
            return false;
        }

    case TRANSLATE_DEFLATED:
        if (!is_valid_absolute_path(payload, payload_length)) {
            Fail("malformed DEFLATED packet");
            return false;
        }

        if (file_address != nullptr) {
            file_address->deflated = payload;
            return true;
        } else if (nfs_address != nullptr) {
            /* ignore for now */
        } else {
            Fail("misplaced DEFLATED packet");
            return false;
        }

    case TRANSLATE_GZIPPED:
        if (!is_valid_absolute_path(payload, payload_length)) {
            Fail("malformed GZIPPED packet");
            return false;
        }

        if (file_address != nullptr) {
            if (file_address->auto_gzipped ||
                file_address->gzipped != nullptr) {
                Fail("misplaced GZIPPED packet");
                return false;
            }

            file_address->gzipped = payload;
            return true;
        } else if (nfs_address != nullptr) {
            /* ignore for now */
        } else {
            Fail("misplaced GZIPPED packet");
            return false;
        }

    case TRANSLATE_SITE:
        assert(resource_address != nullptr);

        if (!is_valid_nonempty_string(payload, payload_length)) {
            Fail("malformed SITE packet");
            return false;
        }

        if (resource_address == &response.address)
            response.site = payload;
        else if (jail != nullptr && jail->enabled)
            jail->site_id = payload;
        else {
            Fail("misplaced SITE packet");
            return false;
        }

        return true;

    case TRANSLATE_CONTENT_TYPE:
        if (!is_valid_nonempty_string(payload, payload_length)) {
            Fail("malformed CONTENT_TYPE packet");
            return false;
        }

        if (!response.content_type_lookup.IsNull()) {
            Fail("CONTENT_TYPE/CONTENT_TYPE_LOOKUP conflict");
            return false;
        }

        if (file_address != nullptr) {
            file_address->content_type = payload;
            return true;
        } else if (nfs_address != nullptr) {
            nfs_address->content_type = payload;
            return true;
        } else if (from_request.content_type_lookup) {
            response.content_type = payload;
            return true;
        } else {
            Fail("misplaced CONTENT_TYPE packet");
            return false;
        }

    case TRANSLATE_HTTP:
        if (resource_address == nullptr ||
            resource_address->type != RESOURCE_ADDRESS_NONE) {
            Fail("misplaced HTTP packet");
            return false;
        }

        if (!is_valid_nonempty_string(payload, payload_length)) {
            Fail("malformed HTTP packet");
            return false;
        }

        resource_address->type = RESOURCE_ADDRESS_HTTP;
        resource_address->u.http = http_address =
            http_address_parse(pool, payload, &error);
        if (http_address == nullptr) {
            Fail(error);
            return false;
        }

        if (http_address->scheme != URI_SCHEME_UNIX &&
            http_address->scheme != URI_SCHEME_HTTP) {
            Fail("malformed HTTP packet");
            return false;
        }

        address_list = &http_address->addresses;
        default_port = http_address->GetDefaultPort();
        return true;

    case TRANSLATE_REDIRECT:
        if (!is_valid_nonempty_string(payload, payload_length)) {
            Fail("malformed REDIRECT packet");
            return false;
        }

        response.redirect = payload;
        return true;

    case TRANSLATE_EXPAND_REDIRECT:
        if (response.regex == nullptr ||
            response.redirect == nullptr ||
            response.expand_redirect != nullptr) {
            Fail("misplaced EXPAND_REDIRECT packet");
            return false;
        }

        if (!is_valid_nonempty_string(payload, payload_length)) {
            Fail("malformed EXPAND_REDIRECT packet");
            return false;
        }

        response.expand_redirect = payload;
        return true;

    case TRANSLATE_BOUNCE:
        if (!is_valid_nonempty_string(payload, payload_length)) {
            Fail("malformed BOUNCE packet");
            return false;
        }

        response.bounce = payload;
        return true;

    case TRANSLATE_FILTER:
        new_transformation = translate_add_transformation(this);
        new_transformation->type = Transformation::Type::FILTER;
        new_transformation->u.filter.type = RESOURCE_ADDRESS_NONE;
        resource_address = &new_transformation->u.filter;
        jail = nullptr;
        child_options = nullptr;
        ns_options = nullptr;
        mount_list = nullptr;
        file_address = nullptr;
        cgi_address = nullptr;
        nfs_address = nullptr;
        lhttp_address = nullptr;
        address_list = nullptr;
        return true;

    case TRANSLATE_FILTER_4XX:
        if (view != nullptr)
            view->filter_4xx = true;
        else
            response.filter_4xx = true;
        return true;

    case TRANSLATE_PROCESS:
        new_transformation = translate_add_transformation(this);
        new_transformation->type = Transformation::Type::PROCESS;
        new_transformation->u.processor.options = PROCESSOR_REWRITE_URL;
        return true;

    case TRANSLATE_DOMAIN:
        daemon_log(2, "deprecated DOMAIN packet\n");
        return true;

    case TRANSLATE_CONTAINER:
        if (transformation == nullptr ||
            transformation->type != Transformation::Type::PROCESS) {
            Fail("misplaced CONTAINER packet");
            return false;
        }

        transformation->u.processor.options |= PROCESSOR_CONTAINER;
        return true;

    case TRANSLATE_SELF_CONTAINER:
        if (transformation == nullptr ||
            transformation->type != Transformation::Type::PROCESS) {
            Fail("misplaced SELF_CONTAINER packet");
            return false;
        }

        transformation->u.processor.options |=
            PROCESSOR_SELF_CONTAINER|PROCESSOR_CONTAINER;
        return true;

    case TRANSLATE_GROUP_CONTAINER:
        if (!is_valid_nonempty_string(payload, payload_length)) {
            Fail("malformed GROUP_CONTAINER packet");
            return false;
        }

        if (transformation == nullptr ||
            transformation->type != Transformation::Type::PROCESS) {
            Fail("misplaced GROUP_CONTAINER packet");
            return false;
        }

        transformation->u.processor.options |= PROCESSOR_CONTAINER;
        strset_add(pool, &response.container_groups, payload);
        return true;

    case TRANSLATE_WIDGET_GROUP:
        if (!is_valid_nonempty_string(payload, payload_length)) {
            Fail("malformed WIDGET_GROUP packet");
            return false;
        }

        response.widget_group = payload;
        return true;

    case TRANSLATE_UNTRUSTED:
        if (!is_valid_nonempty_string(payload, payload_length) || *payload == '.' ||
            payload[payload_length - 1] == '.') {
            Fail("malformed UNTRUSTED packet");
            return false;
        }

        if (response.untrusted_prefix != nullptr ||
            response.untrusted_site_suffix != nullptr) {
            Fail("misplaced UNTRUSTED packet");
            return false;
        }

        response.untrusted = payload;
        return true;

    case TRANSLATE_UNTRUSTED_PREFIX:
        if (!is_valid_nonempty_string(payload, payload_length) || *payload == '.' ||
            payload[payload_length - 1] == '.') {
            Fail("malformed UNTRUSTED_PREFIX packet");
            return false;
        }

        if (response.untrusted != nullptr ||
            response.untrusted_site_suffix != nullptr) {
            Fail("misplaced UNTRUSTED_PREFIX packet");
            return false;
        }

        response.untrusted_prefix = payload;
        return true;

    case TRANSLATE_UNTRUSTED_SITE_SUFFIX:
        if (!is_valid_nonempty_string(payload, payload_length) || *payload == '.' ||
            payload[payload_length - 1] == '.') {
            daemon_log(2, "malformed UNTRUSTED_SITE_SUFFIX packet\n");
            return false;
        }

        if (response.untrusted != nullptr ||
            response.untrusted_prefix != nullptr) {
            daemon_log(2, "misplaced UNTRUSTED_SITE_SUFFIX packet\n");
            return false;
        }

        response.untrusted_site_suffix = payload;
        return true;

    case TRANSLATE_SCHEME:
        if (strncmp(payload, "http", 4) != 0) {
            Fail("misplaced SCHEME packet");
            return false;
        }

        response.scheme = payload;
        return true;

    case TRANSLATE_HOST:
        response.host = payload;
        return true;

    case TRANSLATE_URI:
        if (!is_valid_absolute_uri(payload, payload_length)) {
            Fail("malformed URI packet");
            return false;
        }

        response.uri = payload;
        return true;

    case TRANSLATE_DIRECT_ADDRESSING:
        response.direct_addressing = true;
        return true;

    case TRANSLATE_STATEFUL:
        response.stateful = true;
        return true;

    case TRANSLATE_SESSION:
        response.session = { payload, payload_length };
        return true;

    case TRANSLATE_USER:
        response.user = payload;
        previous_command = command;
        return true;

    case TRANSLATE_REALM:
        response.realm = payload;
        return true;

    case TRANSLATE_LANGUAGE:
        response.language = payload;
        return true;

    case TRANSLATE_PIPE:
        if (resource_address == nullptr ||
            resource_address->type != RESOURCE_ADDRESS_NONE) {
            Fail("misplaced PIPE packet");
            return false;
        }

        if (payload_length == 0) {
            Fail("malformed PIPE packet");
            return false;
        }

        resource_address->type = RESOURCE_ADDRESS_PIPE;
        resource_address->u.cgi = cgi_address =
            cgi_address_new(*pool, payload, false);

        child_options = &cgi_address->options;
        ns_options = &child_options->ns;
        mount_list = &ns_options->mounts;
        jail = &child_options->jail;
        return true;

    case TRANSLATE_CGI:
        if (resource_address == nullptr ||
            resource_address->type != RESOURCE_ADDRESS_NONE) {
            Fail("misplaced CGI packet");
            return false;
        }

        if (!is_valid_absolute_path(payload, payload_length)) {
            Fail("malformed CGI packet");
            return false;
        }

        resource_address->type = RESOURCE_ADDRESS_CGI;
        resource_address->u.cgi = cgi_address =
            cgi_address_new(*pool, payload, false);

        cgi_address->document_root = response.document_root;
        child_options = &cgi_address->options;
        ns_options = &child_options->ns;
        mount_list = &ns_options->mounts;
        jail = &child_options->jail;
        return true;

    case TRANSLATE_FASTCGI:
        if (resource_address == nullptr ||
            resource_address->type != RESOURCE_ADDRESS_NONE) {
            Fail("misplaced FASTCGI packet");
            return false;
        }

        if (!is_valid_absolute_path(payload, payload_length)) {
            Fail("malformed FASTCGI packet");
            return false;
        }

        resource_address->type = RESOURCE_ADDRESS_FASTCGI;
        resource_address->u.cgi = cgi_address =
            cgi_address_new(*pool, payload, true);

        child_options = &cgi_address->options;
        ns_options = &child_options->ns;
        mount_list = &ns_options->mounts;
        jail = &child_options->jail;
        address_list = &cgi_address->address_list;
        default_port = 9000;
        return true;

    case TRANSLATE_AJP:
        if (resource_address == nullptr ||
            resource_address->type != RESOURCE_ADDRESS_NONE) {
            Fail("misplaced AJP packet");
            return false;
        }

        if (payload_length == 0) {
            Fail("malformed AJP packet");
            return false;
        }

        resource_address->type = RESOURCE_ADDRESS_AJP;
        resource_address->u.http = http_address =
            http_address_parse(pool, payload, &error);
        if (http_address == nullptr) {
            Fail(error);
            return false;
        }

        if (http_address->scheme != URI_SCHEME_AJP) {
            Fail("malformed AJP packet");
            return false;
        }

        address_list = &http_address->addresses;
        default_port = 8009;
        return true;

    case TRANSLATE_NFS_SERVER:
        if (resource_address == nullptr ||
            resource_address->type != RESOURCE_ADDRESS_NONE) {
            Fail("misplaced NFS_SERVER packet");
            return false;
        }

        if (payload_length == 0) {
            Fail("malformed NFS_SERVER packet");
            return false;
        }

        resource_address->type = RESOURCE_ADDRESS_NFS;
        resource_address->u.nfs = nfs_address =
            nfs_address_new(*pool, payload, "", "");
        return true;

    case TRANSLATE_NFS_EXPORT:
        if (nfs_address == nullptr ||
            *nfs_address->export_name != 0) {
            Fail("misplaced NFS_EXPORT packet");
            return false;
        }

        if (!is_valid_absolute_path(payload, payload_length)) {
            Fail("malformed NFS_EXPORT packet");
            return false;
        }

        nfs_address->export_name = payload;
        return true;

    case TRANSLATE_JAILCGI:
        if (jail == nullptr) {
            Fail("misplaced JAILCGI packet");
            return false;
        }

        jail->enabled = true;
        return true;

    case TRANSLATE_HOME:
        return translate_client_home(this, payload, payload_length);

    case TRANSLATE_INTERPRETER:
        if (resource_address == nullptr ||
            (resource_address->type != RESOURCE_ADDRESS_CGI &&
             resource_address->type != RESOURCE_ADDRESS_FASTCGI) ||
            cgi_address->interpreter != nullptr) {
            Fail("misplaced INTERPRETER packet");
            return false;
        }

        cgi_address->interpreter = payload;
        return true;

    case TRANSLATE_ACTION:
        if (resource_address == nullptr ||
            (resource_address->type != RESOURCE_ADDRESS_CGI &&
             resource_address->type != RESOURCE_ADDRESS_FASTCGI) ||
            cgi_address->action != nullptr) {
            Fail("misplaced ACTION packet");
            return false;
        }

        cgi_address->action = payload;
        return true;

    case TRANSLATE_SCRIPT_NAME:
        if (resource_address == nullptr ||
            (resource_address->type != RESOURCE_ADDRESS_CGI &&
             resource_address->type != RESOURCE_ADDRESS_WAS &&
             resource_address->type != RESOURCE_ADDRESS_FASTCGI) ||
            cgi_address->script_name != nullptr) {
            Fail("misplaced SCRIPT_NAME packet");
            return false;
        }

        cgi_address->script_name = payload;
        return true;

    case TRANSLATE_EXPAND_SCRIPT_NAME:
        if (!is_valid_nonempty_string(payload, payload_length)) {
            Fail("malformed EXPAND_SCRIPT_NAME packet");
            return false;
        }

        if (response.regex == nullptr ||
            cgi_address == nullptr ||
            cgi_address->expand_script_name != nullptr) {
            Fail("misplaced EXPAND_SCRIPT_NAME packet");
            return false;
        }

        cgi_address->expand_script_name = payload;
        return true;

    case TRANSLATE_DOCUMENT_ROOT:
        if (!is_valid_absolute_path(payload, payload_length)) {
            Fail("malformed DOCUMENT_ROOT packet");
            return false;
        }

        if (cgi_address != nullptr)
            cgi_address->document_root = payload;
        else if (file_address != nullptr &&
                 file_address->delegate != nullptr)
            file_address->document_root = payload;
        else
            response.document_root = payload;
        return true;

    case TRANSLATE_EXPAND_DOCUMENT_ROOT:
        if (!is_valid_nonempty_string(payload, payload_length)) {
            Fail("malformed EXPAND_DOCUMENT_ROOT packet");
            return false;
        }

        if (cgi_address != nullptr)
            cgi_address->expand_document_root = payload;
        else if (file_address != nullptr &&
                 file_address->delegate != nullptr)
            file_address->expand_document_root = payload;
        else
            response.expand_document_root = payload;
        return true;

    case TRANSLATE_ADDRESS:
        if (address_list == nullptr) {
            Fail("misplaced ADDRESS packet");
            return false;
        }

        if (payload_length < 2) {
            Fail("malformed INTERPRETER packet");
            return false;
        }

        address_list->Add(pool,
                          SocketAddress((const struct sockaddr *)_payload,
                                        payload_length));
        return true;


    case TRANSLATE_ADDRESS_STRING:
        if (address_list == nullptr) {
            Fail("misplaced ADDRESS_STRING packet");
            return false;
        }

        if (payload_length == 0) {
            Fail("malformed ADDRESS_STRING packet");
            return false;
        }

        {
            bool ret;

            ret = parse_address_string(pool, address_list,
                                       payload, default_port);
            if (!ret) {
                Fail(    "malformed ADDRESS_STRING packet");
                return false;
            }
        }

        return true;

    case TRANSLATE_VIEW:
        if (!valid_view_name(payload)) {
            Fail("invalid view name");
            return false;
        }

        if (!AddView(payload, &error)) {
            Fail(error);
            return false;
        }

        return true;

    case TRANSLATE_MAX_AGE:
        if (payload_length != 4) {
            Fail("malformed MAX_AGE packet");
            return false;
        }

        switch (previous_command) {
        case TRANSLATE_BEGIN:
            response.max_age = *(const uint32_t *)_payload;
            break;

        case TRANSLATE_USER:
            response.user_max_age = *(const uint32_t *)_payload;
            break;

        default:
            Fail("misplaced MAX_AGE packet");
            return false;
        }

        return true;

    case TRANSLATE_VARY:
        if (payload_length == 0 ||
            payload_length % sizeof(response.vary.data[0]) != 0) {
            Fail("malformed VARY packet");
            return false;
        }

        response.vary.data = (const uint16_t *)_payload;
        response.vary.size = payload_length / sizeof(response.vary.data[0]);
        return true;

    case TRANSLATE_INVALIDATE:
        if (payload_length == 0 ||
            payload_length % sizeof(response.invalidate.data[0]) != 0) {
            Fail("malformed INVALIDATE packet");
            return false;
        }

        response.invalidate.data = (const uint16_t *)_payload;
        response.invalidate.size = payload_length /
            sizeof(response.invalidate.data[0]);
        return true;

    case TRANSLATE_BASE:
        if (!is_valid_absolute_uri(payload, payload_length) ||
            payload[payload_length - 1] != '/') {
            Fail("malformed BASE packet");
            return false;
        }

        if (from_request.uri == nullptr ||
            response.auto_base ||
            response.base != nullptr) {
            Fail("misplaced BASE packet");
            return false;
        }

        if (memcmp(from_request.uri, payload, payload_length) != 0) {
            Fail("BASE mismatches request URI");
            return false;
        }

        response.base = payload;
        return true;

    case TRANSLATE_UNSAFE_BASE:
        if (payload_length > 0) {
            Fail("malformed UNSAFE_BASE packet");
            return false;
        }

        if (response.base == nullptr) {
            Fail("misplaced UNSAFE_BASE packet");
            return false;
        }

        response.unsafe_base = true;
        return true;

    case TRANSLATE_EASY_BASE:
        if (payload_length > 0) {
            Fail("malformed EASY_BASE");
            return false;
        }

        if (response.base == nullptr) {
            Fail("EASY_BASE without BASE");
            return false;
        }

        if (response.easy_base) {
            Fail("duplicate EASY_BASE");
            return false;
        }

        response.easy_base = true;
        return true;

    case TRANSLATE_REGEX:
        if (response.base == nullptr) {
            Fail("REGEX without BASE");
            return false;
        }

        if (response.regex != nullptr) {
            Fail("duplicate REGEX");
            return false;
        }

        if (!is_valid_nonempty_string(payload, payload_length)) {
            Fail("malformed REGEX packet");
            return false;
        }

        response.regex = payload;
        return true;

    case TRANSLATE_INVERSE_REGEX:
        if (response.base == nullptr) {
            Fail("INVERSE_REGEX without BASE");
            return false;
        }

        if (response.inverse_regex != nullptr) {
            Fail("duplicate INVERSE_REGEX");
            return false;
        }

        if (!is_valid_nonempty_string(payload, payload_length)) {
            Fail("malformed INVERSE_REGEX packet");
            return false;
        }

        response.inverse_regex = payload;
        return true;

    case TRANSLATE_REGEX_TAIL:
        if (payload_length > 0) {
            Fail("malformed REGEX_TAIL packet");
            return false;
        }

        if (response.regex == nullptr &&
            response.inverse_regex == nullptr) {
            Fail("misplaced REGEX_TAIL packet");
            return false;
        }

        if (response.regex_tail) {
            Fail("duplicate REGEX_TAIL packet");
            return false;
        }

        response.regex_tail = true;
        return true;

    case TRANSLATE_REGEX_UNESCAPE:
        if (payload_length > 0) {
            Fail("malformed REGEX_UNESCAPE packet");
            return false;
        }

        if (response.regex == nullptr &&
            response.inverse_regex == nullptr) {
            Fail("misplaced REGEX_UNESCAPE packet");
            return false;
        }

        if (response.regex_unescape) {
            Fail("duplicate REGEX_UNESCAPE packet");
            return false;
        }

        response.regex_unescape = true;
        return true;

    case TRANSLATE_DELEGATE:
        if (file_address == nullptr) {
            Fail("misplaced DELEGATE packet");
            return false;
        }

        if (!is_valid_absolute_path(payload, payload_length)) {
            Fail("malformed DELEGATE packet");
            return false;
        }

        file_address->delegate = payload;
        child_options = &file_address->child_options;
        ns_options = &child_options->ns;
        mount_list = &ns_options->mounts;
        jail = &child_options->jail;
        return true;

    case TRANSLATE_APPEND:
        if (!is_valid_nonempty_string(payload, payload_length)) {
            Fail("malformed APPEND packet");
            return false;
        }

        if (resource_address == nullptr) {
            Fail("misplaced APPEND packet");
            return false;
        }

        if (cgi_address != nullptr) {
            if (cgi_address->args.IsFull()) {
                Fail("too many APPEND packets");
                return false;
            }

            cgi_address->args.Append(payload);
            return true;
        } else if (lhttp_address != nullptr) {
            if (lhttp_address->args.IsFull()) {
                Fail("too many APPEND packets");
                return false;
            }

            lhttp_address->args.Append(payload);
            return true;
        } else {
            Fail("misplaced APPEND packet");
            return false;
        }

    case TRANSLATE_EXPAND_APPEND:
        if (!is_valid_nonempty_string(payload, payload_length)) {
            Fail("malformed EXPAND_APPEND packet");
            return false;
        }

        if (response.regex == nullptr ||
            resource_address == nullptr) {
            Fail("misplaced EXPAND_APPEND packet");
            return false;
        }

        if (cgi_address != nullptr) {
            if (!cgi_address->args.CanSetExpand()) {
                Fail("misplaced EXPAND_APPEND packet");
                return false;
            }

            cgi_address->args.SetExpand(payload);
            return true;
        } else if (lhttp_address != nullptr) {
            if (!lhttp_address->args.CanSetExpand()) {
                Fail("misplaced EXPAND_APPEND packet");
                return false;
            }

            lhttp_address->args.SetExpand(payload);
            return true;
        } else {
            Fail("misplaced APPEND packet");
            return false;
        }

    case TRANSLATE_PAIR:
        if (cgi_address != nullptr) {
            const auto type = resource_address->type;
            struct param_array &p = type == RESOURCE_ADDRESS_CGI ||
                type == RESOURCE_ADDRESS_PIPE
                ? cgi_address->env
                : cgi_address->params;

            return translate_client_pair(this, p, "PAIR",
                                         payload, payload_length);
        } else if (lhttp_address != nullptr) {
            return translate_client_pair(this, lhttp_address->env,
                                         "PAIR", payload, payload_length);
        } else {
            Fail("misplaced PAIR packet");
            return false;
        }

    case TRANSLATE_EXPAND_PAIR:
        if (response.regex == nullptr) {
            Fail("misplaced EXPAND_PAIR packet");
            return false;
        }

        if (cgi_address != nullptr) {
            const auto type = resource_address->type;
            struct param_array &p = type == RESOURCE_ADDRESS_CGI
                ? cgi_address->env
                : cgi_address->params;

            return translate_client_expand_pair(this, p, "EXPAND_PAIR",
                                                payload, payload_length);
        } else if (lhttp_address != nullptr) {
            return translate_client_expand_pair(this,
                                                lhttp_address->env,
                                                "EXPAND_PAIR",
                                                payload, payload_length);
        } else {
            Fail("misplaced EXPAND_PAIR packet");
            return false;
        }

    case TRANSLATE_DISCARD_SESSION:
        response.discard_session = true;
        return true;

    case TRANSLATE_REQUEST_HEADER_FORWARD:
        return view != nullptr
            ? parse_header_forward(*this,
                                   &view->request_header_forward,
                                   payload, payload_length)
            : parse_header_forward(*this,
                                   &response.request_header_forward,
                                   payload, payload_length);

    case TRANSLATE_RESPONSE_HEADER_FORWARD:
        return view != nullptr
            ? parse_header_forward(*this,
                                   &view->response_header_forward,
                                   payload, payload_length)
            : parse_header_forward(*this,
                                   &response.response_header_forward,
                                   payload, payload_length);

    case TRANSLATE_WWW_AUTHENTICATE:
        if (!is_valid_nonempty_string(payload, payload_length)) {
            Fail("malformed WWW_AUTHENTICATE packet");
            return false;
        }

        response.www_authenticate = payload;
        return true;

    case TRANSLATE_AUTHENTICATION_INFO:
        if (!is_valid_nonempty_string(payload, payload_length)) {
            Fail("malformed AUTHENTICATION_INFO packet");
            return false;
        }

        response.authentication_info = payload;
        return true;

    case TRANSLATE_HEADER:
        if (!parse_header(pool, response.response_headers,
                          "HEADER", payload, payload_length, &error)) {
            Fail(error);
            return false;
        }

        return true;

    case TRANSLATE_SECURE_COOKIE:
        response.secure_cookie = true;
        return true;

    case TRANSLATE_COOKIE_DOMAIN:
        if (response.cookie_domain != nullptr) {
            Fail("misplaced COOKIE_DOMAIN packet");
            return false;
        }

        if (!is_valid_nonempty_string(payload, payload_length)) {
            Fail("malformed COOKIE_DOMAIN packet");
            return false;
        }

        response.cookie_domain = payload;
        return true;

    case TRANSLATE_ERROR_DOCUMENT:
        response.error_document = { payload, payload_length };
        return true;

    case TRANSLATE_CHECK:
        if (!response.check.IsNull()) {
            Fail("duplicate CHECK packet");
            return false;
        }

        response.check = { payload, payload_length };
        return true;

    case TRANSLATE_PREVIOUS:
        response.previous = true;
        return true;

    case TRANSLATE_WAS:
        if (resource_address == nullptr ||
            resource_address->type != RESOURCE_ADDRESS_NONE) {
            Fail("misplaced WAS packet");
            return false;
        }

        if (!is_valid_absolute_path(payload, payload_length)) {
            Fail("malformed WAS packet");
            return false;
        }

        resource_address->type = RESOURCE_ADDRESS_WAS;
        resource_address->u.cgi = cgi_address =
            cgi_address_new(*pool, payload, false);

        child_options = &cgi_address->options;
        ns_options = &child_options->ns;
        mount_list = &ns_options->mounts;
        jail = &child_options->jail;
        return true;

    case TRANSLATE_TRANSPARENT:
        response.transparent = true;
        return true;

    case TRANSLATE_WIDGET_INFO:
        response.widget_info = true;
        return true;

    case TRANSLATE_STICKY:
        if (address_list == nullptr) {
            Fail("misplaced STICKY packet");
            return false;
        }

        address_list->SetStickyMode(STICKY_SESSION_MODULO);
        return true;

    case TRANSLATE_DUMP_HEADERS:
        response.dump_headers = true;
        return true;

    case TRANSLATE_COOKIE_HOST:
        if (resource_address == nullptr ||
            resource_address->type == RESOURCE_ADDRESS_NONE) {
            Fail("misplaced COOKIE_HOST packet");
            return false;
        }

        if (!is_valid_nonempty_string(payload, payload_length)) {
            Fail("malformed COOKIE_HOST packet");
            return false;
        }

        response.cookie_host = payload;
        return true;

    case TRANSLATE_COOKIE_PATH:
        if (response.cookie_path != nullptr) {
            Fail("misplaced COOKIE_PATH packet");
            return false;
        }

        if (!is_valid_absolute_uri(payload, payload_length)) {
            Fail("malformed COOKIE_PATH packet");
            return false;
        }

        response.cookie_path = payload;
        return true;

    case TRANSLATE_PROCESS_CSS:
        new_transformation = translate_add_transformation(this);
        new_transformation->type = Transformation::Type::PROCESS_CSS;
        new_transformation->u.css_processor.options = CSS_PROCESSOR_REWRITE_URL;
        return true;

    case TRANSLATE_PREFIX_CSS_CLASS:
        if (transformation == nullptr) {
            Fail("misplaced PREFIX_CSS_CLASS packet");
            return false;
        }

        switch (transformation->type) {
        case Transformation::Type::PROCESS:
            transformation->u.processor.options |= PROCESSOR_PREFIX_CSS_CLASS;
            break;

        case Transformation::Type::PROCESS_CSS:
            transformation->u.css_processor.options |= CSS_PROCESSOR_PREFIX_CLASS;
            break;

        default:
            Fail("misplaced PREFIX_CSS_CLASS packet");
            return false;
        }

        return true;

    case TRANSLATE_PREFIX_XML_ID:
        if (transformation == nullptr) {
            Fail("misplaced PREFIX_XML_ID packet");
            return false;
        }

        switch (transformation->type) {
        case Transformation::Type::PROCESS:
            transformation->u.processor.options |= PROCESSOR_PREFIX_XML_ID;
            break;

        case Transformation::Type::PROCESS_CSS:
            transformation->u.css_processor.options |= CSS_PROCESSOR_PREFIX_ID;
            break;

        default:
            Fail("misplaced PREFIX_XML_ID packet");
            return false;
        }

        return true;

    case TRANSLATE_PROCESS_STYLE:
        if (transformation == nullptr ||
            transformation->type != Transformation::Type::PROCESS) {
            Fail("misplaced PROCESS_STYLE packet");
            return false;
        }

        transformation->u.processor.options |= PROCESSOR_STYLE;
        return true;

    case TRANSLATE_FOCUS_WIDGET:
        if (transformation == nullptr ||
            transformation->type != Transformation::Type::PROCESS) {
            Fail("misplaced FOCUS_WIDGET packet");
            return false;
        }

        transformation->u.processor.options |= PROCESSOR_FOCUS_WIDGET;
        return true;

    case TRANSLATE_ANCHOR_ABSOLUTE:
        if (transformation == nullptr ||
            transformation->type != Transformation::Type::PROCESS) {
            Fail("misplaced ANCHOR_ABSOLUTE packet");
            return false;
        }

        response.anchor_absolute = true;
        return true;

    case TRANSLATE_PROCESS_TEXT:
        new_transformation = translate_add_transformation(this);
        new_transformation->type = Transformation::Type::PROCESS_TEXT;
        return true;

    case TRANSLATE_LOCAL_URI:
        if (response.local_uri != nullptr) {
            Fail("misplaced LOCAL_URI packet");
            return false;
        }

        if (!is_valid_absolute_uri(payload, payload_length) ||
            payload[payload_length - 1] != '/') {
            Fail("malformed LOCAL_URI packet");
            return false;
        }

        response.local_uri = payload;
        return true;

    case TRANSLATE_AUTO_BASE:
        if (resource_address != &response.address ||
            cgi_address == nullptr ||
            cgi_address != response.address.u.cgi ||
            cgi_address->path_info == nullptr ||
            from_request.uri == nullptr ||
            response.base != nullptr ||
            response.auto_base) {
            Fail("misplaced AUTO_BASE packet");
            return false;
        }

        response.auto_base = true;
        return true;

    case TRANSLATE_VALIDATE_MTIME:
        if (payload_length < 10 || payload[8] != '/' ||
            memchr(payload + 9, 0, payload_length - 9) != nullptr) {
            Fail("malformed VALIDATE_MTIME packet");
            return false;
        }

        response.validate_mtime.mtime = *(const uint64_t *)_payload;
        response.validate_mtime.path =
            p_strndup(pool, payload + 8, payload_length - 8);
        return true;

    case TRANSLATE_LHTTP_PATH:
        if (resource_address == nullptr ||
            resource_address->type != RESOURCE_ADDRESS_NONE) {
            Fail("misplaced LHTTP_PATH packet");
            return false;
        }

        if (!is_valid_absolute_path(payload, payload_length)) {
            Fail("malformed LHTTP_PATH packet");
            return false;
        }

        resource_address->type = RESOURCE_ADDRESS_LHTTP;
        resource_address->u.lhttp = lhttp_address =
            lhttp_address_new(*pool, payload);
        child_options = &lhttp_address->options;
        ns_options = &child_options->ns;
        mount_list = &ns_options->mounts;
        jail = &child_options->jail;
        return true;

    case TRANSLATE_LHTTP_URI:
        if (lhttp_address == nullptr ||
            lhttp_address->uri != nullptr) {
            Fail("misplaced LHTTP_HOST packet");
            return false;
        }

        if (!is_valid_absolute_uri(payload, payload_length)) {
            Fail("malformed LHTTP_URI packet");
            return false;
        }

        lhttp_address->uri = payload;
        return true;

    case TRANSLATE_EXPAND_LHTTP_URI:
        if (lhttp_address == nullptr ||
            lhttp_address->uri == nullptr ||
            lhttp_address->expand_uri != nullptr ||
            response.regex == nullptr) {
            Fail("misplaced EXPAND_LHTTP_URI packet");
            return false;
        }

        if (!is_valid_nonempty_string(payload, payload_length)) {
            Fail("malformed EXPAND_LHTTP_URI packet");
            return false;
        }

        lhttp_address->expand_uri = payload;
        return true;

    case TRANSLATE_LHTTP_HOST:
        if (lhttp_address == nullptr ||
            lhttp_address->host_and_port != nullptr) {
            Fail("misplaced LHTTP_HOST packet");
            return false;
        }

        if (!is_valid_nonempty_string(payload, payload_length)) {
            Fail("malformed LHTTP_HOST packet");
            return false;
        }

        lhttp_address->host_and_port = payload;
        return true;

    case TRANSLATE_CONCURRENCY:
        if (lhttp_address == nullptr) {
            Fail("misplaced CONCURRENCY packet");
            return false;
        }

        if (payload_length != 2) {
            Fail("malformed CONCURRENCY packet");
            return false;
        }

        lhttp_address->concurrency = *(const uint16_t *)_payload;
        return true;

    case TRANSLATE_WANT_FULL_URI:
        if (from_request.want_full_uri) {
            Fail("WANT_FULL_URI loop");
            return false;
        }

        if (!response.want_full_uri.IsNull()) {
            Fail("duplicate WANT_FULL_URI packet");
            return false;
        }

        response.want_full_uri = { payload, payload_length };
        return true;

    case TRANSLATE_USER_NAMESPACE:
        if (payload_length != 0) {
            Fail("malformed USER_NAMESPACE packet");
            return false;
        }

        if (ns_options != nullptr) {
            ns_options->enable_user = true;
        } else {
            Fail("misplaced USER_NAMESPACE packet");
            return false;
        }

        return true;

    case TRANSLATE_PID_NAMESPACE:
        if (payload_length != 0) {
            Fail("malformed PID_NAMESPACE packet");
            return false;
        }

        if (ns_options != nullptr) {
            ns_options->enable_pid = true;
        } else {
            Fail("misplaced PID_NAMESPACE packet");
            return false;
        }

        return true;

    case TRANSLATE_NETWORK_NAMESPACE:
        if (payload_length != 0) {
            Fail("malformed NETWORK_NAMESPACE packet");
            return false;
        }

        if (ns_options != nullptr) {
            ns_options->enable_network = true;
        } else {
            Fail("misplaced NETWORK_NAMESPACE packet");
            return false;
        }

        return true;

    case TRANSLATE_PIVOT_ROOT:
        return translate_client_pivot_root(this, payload, payload_length);

    case TRANSLATE_MOUNT_PROC:
        return translate_client_mount_proc(this, payload_length);

    case TRANSLATE_MOUNT_HOME:
        return translate_client_mount_home(this, payload, payload_length);

    case TRANSLATE_BIND_MOUNT:
        return translate_client_bind_mount(this, payload, payload_length);

    case TRANSLATE_MOUNT_TMP_TMPFS:
        return translate_client_mount_tmp_tmpfs(this, payload_length);

    case TRANSLATE_UTS_NAMESPACE:
        return translate_client_uts_namespace(this, payload);

    case TRANSLATE_RLIMITS:
        return translate_client_rlimits(this, payload);

    case TRANSLATE_WANT:
        return translate_client_want(this, (const uint16_t *)_payload,
                                     payload_length);

    case TRANSLATE_FILE_NOT_FOUND:
        return translate_client_file_not_found(this,
                                               { _payload, payload_length });

    case TRANSLATE_CONTENT_TYPE_LOOKUP:
        return translate_client_content_type_lookup(*this,
                                                    { _payload, payload_length });

    case TRANSLATE_DIRECTORY_INDEX:
        return translate_client_directory_index(*this,
                                                { _payload, payload_length });

    case TRANSLATE_EXPIRES_RELATIVE:
        return translate_client_expires_relative(*this,
                                                 { _payload, payload_length });


    case TRANSLATE_TEST_PATH:
        if (!is_valid_absolute_path(payload, payload_length)) {
            Fail("malformed TEST_PATH packet");
            return false;
        }

        if (response.test_path != nullptr) {
            Fail("duplicate TEST_PATH packet");
            return false;
        }

        response.test_path = payload;
        return true;

    case TRANSLATE_EXPAND_TEST_PATH:
        if (response.regex == nullptr) {
            Fail("misplaced EXPAND_TEST_PATH packet");
            return false;
        }

        if (!is_valid_nonempty_string(payload, payload_length)) {
            Fail("malformed EXPAND_TEST_PATH packet");
            return false;
        }

        if (response.expand_test_path != nullptr) {
            Fail("duplicate EXPAND_TEST_PATH packet");
            return false;
        }

        response.expand_test_path = payload;
        return true;

    case TRANSLATE_REDIRECT_QUERY_STRING:
        if (payload_length != 0) {
            Fail("malformed REDIRECT_QUERY_STRING packet");
            return false;
        }

        if (response.redirect_query_string ||
            (response.redirect == nullptr &&
             response.expand_redirect == nullptr)) {
            Fail("misplaced REDIRECT_QUERY_STRING packet");
            return false;
        }

        response.redirect_query_string = true;
        return true;

    case TRANSLATE_ENOTDIR:
        return translate_client_enotdir(*this, { _payload, payload_length });

    case TRANSLATE_STDERR_PATH:
        return translate_client_stderr_path(*this,
                                            { _payload, payload_length });

    case TRANSLATE_AUTH:
        if (response.HasAuth()) {
            Fail("duplicate AUTH packet");
            return false;
        }

        response.auth = { payload, payload_length };
        return true;

    case TRANSLATE_SETENV:
        if (cgi_address != nullptr) {
            return translate_client_pair(this, cgi_address->env,
                                         "SETENV",
                                         payload, payload_length);
        } else if (lhttp_address != nullptr) {
            return translate_client_pair(this, lhttp_address->env,
                                         "SETENV", payload, payload_length);
        } else {
            Fail("misplaced SETENV packet");
            return false;
        }

    case TRANSLATE_EXPAND_SETENV:
        if (response.regex == nullptr) {
            Fail("misplaced EXPAND_SETENV packet");
            return false;
        }

        if (cgi_address != nullptr) {
            return translate_client_expand_pair(this,
                                                cgi_address->env,
                                                "EXPAND_SETENV",
                                                payload, payload_length);
        } else if (lhttp_address != nullptr) {
            return translate_client_expand_pair(this,
                                                lhttp_address->env,
                                                "EXPAND_SETENV",
                                                payload, payload_length);
        } else {
            Fail("misplaced SETENV packet");
            return false;
        }

    case TRANSLATE_EXPAND_URI:
        if (response.regex == nullptr ||
            response.uri == nullptr ||
            response.expand_uri != nullptr) {
            Fail("misplaced EXPAND_URI packet");
            return false;
        }

        if (!is_valid_nonempty_string(payload, payload_length)) {
            Fail("malformed EXPAND_URI packet");
            return false;
        }

        response.expand_uri = payload;
        return true;

    case TRANSLATE_EXPAND_SITE:
        if (response.regex == nullptr ||
            response.site == nullptr ||
            response.expand_site != nullptr) {
            Fail("misplaced EXPAND_SITE packet");
            return false;
        }

        if (!is_valid_nonempty_string(payload, payload_length)) {
            Fail("malformed EXPAND_SITE packet");
            return false;
        }

        response.expand_site = payload;
        return true;

    case TRANSLATE_REQUEST_HEADER:
        if (!parse_header(pool, response.request_headers,
                          "REQUEST_HEADER", payload, payload_length, &error)) {
            Fail(error);
            return false;
        }

        return true;

    case TRANSLATE_EXPAND_REQUEST_HEADER:
        if (response.regex == nullptr) {
            Fail("misplaced EXPAND_REQUEST_HEADERS packet");
            return false;
        }

        if (!parse_header(pool,
                          response.expand_request_headers,
                          "EXPAND_REQUEST_HEADER", payload, payload_length,
                          &error)) {
            Fail(error);
            return false;
        }

        return true;

    case TRANSLATE_AUTO_GZIPPED:
        if (payload_length > 0) {
            Fail("malformed AUTO_GZIPPED packet");
            return false;
        }

        if (file_address != nullptr) {
            if (file_address->auto_gzipped ||
                file_address->gzipped != nullptr) {
                Fail("misplaced AUTO_GZIPPED packet");
                return false;
            }

            file_address->auto_gzipped = true;
            return true;
        } else if (nfs_address != nullptr) {
            /* ignore for now */
        } else {
            Fail("misplaced AUTO_GZIPPED packet");
            return false;
        }

    case TRANSLATE_PROBE_PATH_SUFFIXES:
        if (!response.probe_path_suffixes.IsNull() ||
            (response.test_path == nullptr &&
             response.expand_test_path == nullptr)) {
            Fail("misplaced PROBE_PATH_SUFFIXES packet");
            return false;
        }

        response.probe_path_suffixes = { payload, payload_length };
        return true;

    case TRANSLATE_PROBE_SUFFIX:
        if (response.probe_path_suffixes.IsNull()) {
            Fail("misplaced PROBE_SUFFIX packet");
            return false;
        }

        if (response.probe_suffixes.full()) {
            Fail("too many PROBE_SUFFIX packets");
            return false;
        }

        if (!CheckProbeSuffix(payload, payload_length)) {
            Fail("malformed PROBE_SUFFIX packets");
            return false;
        }

        response.probe_suffixes.push_back(payload);
        return true;

    case TRANSLATE_AUTH_FILE:
        if (response.HasAuth()) {
            Fail("duplicate AUTH_FILE packet");
            return false;
        }

        if (!is_valid_absolute_path(payload, payload_length)) {
            Fail("malformed AUTH_FILE packet");
            return false;
        }

        response.auth_file = payload;
        return true;

    case TRANSLATE_EXPAND_AUTH_FILE:
        if (response.HasAuth()) {
            Fail("duplicate EXPAND_AUTH_FILE packet");
            return false;
        }

        if (!is_valid_nonempty_string(payload, payload_length)) {
            Fail("malformed EXPAND_AUTH_FILE packet");
            return false;
        }

        response.expand_auth_file = payload;
        return true;

    case TRANSLATE_APPEND_AUTH:
        if (!response.HasAuth() ||
            !response.append_auth.IsNull() ||
            response.expand_append_auth != nullptr) {
            Fail("misplaced APPEND_AUTH packet");
            return false;
        }

        response.append_auth = { payload, payload_length };
        return true;

    case TRANSLATE_EXPAND_APPEND_AUTH:
        if (!response.HasAuth() ||
            !response.append_auth.IsNull() ||
            response.expand_append_auth != nullptr) {
            Fail("misplaced EXPAND_APPEND_AUTH packet");
            return false;
        }

        if (!is_valid_nonempty_string(payload, payload_length)) {
            Fail("malformed EXPAND_APPEND_AUTH packet");
            return false;
        }

        response.expand_append_auth = payload;
        return true;

    case TRANSLATE_EXPAND_COOKIE_HOST:
        if (resource_address == nullptr ||
            resource_address->type == RESOURCE_ADDRESS_NONE) {
            Fail("misplaced EXPAND_COOKIE_HOST packet");
            return false;
        }

        if (!is_valid_nonempty_string(payload, payload_length)) {
            Fail("malformed EXPAND_COOKIE_HOST packet");
            return false;
        }

        response.expand_cookie_host = payload;
        return true;
    }

    error = g_error_new(translate_quark(), 0,
                        "unknown translation packet: %u", command);
    Fail(error);
    return false;
}

inline BufferedResult
TranslateClient::Feed(const uint8_t *data, size_t length)
{
    size_t consumed = 0;
    while (consumed < length) {
        size_t nbytes = packet_reader_feed(pool, &reader,
                                           data + consumed, length - consumed);
        if (nbytes == 0)
            /* need more data */
            break;

        consumed += nbytes;
        socket.Consumed(nbytes);

        if (reader.state != TranslatePacketReader::State::COMPLETE)
            /* need more data */
            break;

        if (!HandlePacket((enum beng_translation_command)reader.header.command,
                                  reader.payload == nullptr
                                  ? "" : reader.payload,
                                  reader.header.length))
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
    auto src = client->request.Read();
    assert(!src.IsNull());

    ssize_t nbytes = client->socket.Write(src.data, src.size);
    if (gcc_unlikely(nbytes < 0)) {
        if (gcc_likely(nbytes == WRITE_BLOCKING))
            return true;

        GError *error =
            new_error_errno_msg("write error to translation server");
        client->Fail(error);
        return false;
    }

    client->request.Consume(nbytes);
    if (client->request.IsEOF()) {
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

    return client->Feed((const uint8_t *)buffer, size);
}

static bool
translate_client_socket_closed(void *ctx)
{
    TranslateClient *client = (TranslateClient *)ctx;

    client->ReleaseSocket(false);
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
    client->Fail(error);
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
    client->ReleaseSocket(false);
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
    GrowingBuffer *gb = marshal_request(pool, request, &error);
    if (gb == nullptr) {
        lease->Release(lease_ctx, true);

        handler->error(error, ctx);
        pool_unref(pool);
        return;
    }

    TranslateClient *client = NewFromPool<TranslateClient>(*pool, *gb);
    client->pool = pool;
    client->stopwatch = stopwatch_fd_new(pool, fd,
                                         request->uri != nullptr ? request->uri
                                         : request->widget_type);
    client->socket.Init(*pool, fd, ISTREAM_SOCKET,
                        &translate_read_timeout,
                        &translate_write_timeout,
                        translate_client_socket_handler, client);
    p_lease_ref_set(client->lease_ref, *lease, lease_ctx,
                    *pool, "translate_lease");

    client->from_request.uri = request->uri;
    client->from_request.want_full_uri = !request->want_full_uri.IsNull();
    client->from_request.want = !request->want.IsEmpty();
    client->from_request.content_type_lookup =
        !request->content_type_lookup.IsNull();

    client->handler = handler;
    client->handler_ctx = ctx;
    client->response.status = (http_status_t)-1;

    client->async.Init(translate_operation);
    async_ref->Set(client->async);

    pool_ref(client->pool);
    translate_try_write(client);
}
