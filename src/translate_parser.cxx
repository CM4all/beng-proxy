/*
 * Parse translation response packets.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "translate_parser.hxx"
#include "translate_quark.hxx"
#include "transformation.hxx"
#include "widget_class.hxx"
#include "processor.hxx"
#include "css_processor.hxx"
#include "file_address.hxx"
#include "delegate/Address.hxx"
#include "lhttp_address.hxx"
#include "http_address.hxx"
#include "cgi_address.hxx"
#include "nfs_address.hxx"
#include "spawn/mount_list.hxx"
#include "beng-proxy/translation.h"
#include "pool.hxx"
#include "net/SocketAddress.hxx"
#include "util/CharUtil.hxx"

#include <daemon/log.h>
#include <socket/resolver.h>
#include <http/header.h>

#include <glib.h>

#include <assert.h>
#include <string.h>
#include <sys/un.h>
#include <netdb.h>

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

Transformation *
TranslateParser::AddTransformation()
{
    auto t = NewFromPool<Transformation>(*pool);
    t->next = nullptr;

    transformation = t;
    *transformation_tail = t;
    transformation_tail = &t->next;

    return t;
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

bool
TranslateParser::FinishView(GError **error_r)
{
    assert(response.views != nullptr);

    WidgetView *v = view;
    if (view == nullptr) {
        v = response.views;
        assert(v != nullptr);

        const ResourceAddress *address = &response.address;
        if (address->type != ResourceAddress::Type::NONE &&
            v->address.type == ResourceAddress::Type::NONE) {
            /* no address yet: copy address from response */
            v->address.CopyFrom(*pool, *address);
            v->filter_4xx = response.filter_4xx;
        }

        v->request_header_forward = response.request_header_forward;
        v->response_header_forward = response.response_header_forward;
    } else {
        if (v->address.type == ResourceAddress::Type::NONE && v != response.views)
            /* no address yet: inherits settings from the default view */
            v->InheritFrom(*pool, *response.views);
    }

    if (!v->address.Check(error_r))
        return false;

    return true;
}

inline bool
TranslateParser::AddView(const char *name, GError **error_r)
{
    if (!FinishView(error_r))
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
parse_header_forward(struct header_forward_settings *settings,
                     const void *payload, size_t payload_length,
                     GError **error_r)
{
    const beng_header_forward_packet *packet =
        (const beng_header_forward_packet *)payload;

    if (payload_length % sizeof(*packet) != 0) {
        g_set_error_literal(error_r, translate_quark(), 0,
                            "malformed header forward packet");
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
            g_set_error_literal(error_r, translate_quark(), 0,
                                "malformed header forward packet");
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

    const char *name = p_strdup_lower(*pool, StringView(payload, value));
    ++value;

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
translate_jail_finish(JailParams *jail,
                      const TranslateResponse *response,
                      const char *document_root,
                      GError **error_r)
{
    if (!jail->enabled)
        return true;

    if (jail->home_directory == nullptr)
        jail->home_directory = document_root;

    if (jail->home_directory == nullptr) {
        g_set_error_literal(error_r, translate_quark(), 0,
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
    if (response->easy_base && !response->address.IsValidBase()) {
        /* EASY_BASE was enabled, but the resource address does not
           end with a slash, thus LoadBase() cannot work */
        g_set_error_literal(error_r, translate_quark(), 0,
                            "Invalid base address");
        return false;
    }

    if (response->address.IsCgiAlike()) {
        auto &cgi = response->address.GetCgi();

        if (cgi.uri == nullptr)
            cgi.uri = response->uri;

        if (cgi.expand_uri == nullptr)
            cgi.expand_uri = response->expand_uri;

        if (cgi.document_root == nullptr)
            cgi.document_root = response->document_root;

        if (!translate_jail_finish(&cgi.options.jail,
                                   response, cgi.document_root,
                                   error_r))
            return false;
    } else if (response->address.type == ResourceAddress::Type::LOCAL) {
        auto &file = response->address.GetFile();

        if (file.delegate != nullptr) {
            if (file.delegate->child_options.jail.enabled &&
                file.document_root == nullptr)
                file.document_root = response->document_root;

            if (!translate_jail_finish(&file.delegate->child_options.jail,
                                       response,
                                       file.document_root,
                                       error_r))
                return false;
        }
    }

    if (!response->address.Check(error_r))
        return false;

    /* these lists are in reverse order because new items were added
       to the front; reverse them now */
    response->request_headers.Reverse();
    response->response_headers.Reverse();

    if (!response->probe_path_suffixes.IsNull() &&
        response->probe_suffixes.empty()) {
        g_set_error_literal(error_r, translate_quark(), 0,
                            "PROBE_PATH_SUFFIX without PROBE_SUFFIX");
        return false;
    }

    if (!response->internal_redirect.IsNull() &&
        (response->uri == nullptr && response->expand_uri == nullptr)) {
        g_set_error_literal(error_r, translate_quark(), 0,
                            "INTERNAL_REDIRECT without URI");
        return false;
    }

    if (!response->internal_redirect.IsNull() &&
        !response->want_full_uri.IsNull()) {
        g_set_error_literal(error_r, translate_quark(), 0,
                            "INTERNAL_REDIRECT conflicts with WANT_FULL_URI");
        return false;
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
translate_client_check_pair(const char *name,
                            const char *payload, size_t payload_length,
                            GError **error_r)
{
    if (!translate_client_check_pair(payload, payload_length)) {
        g_set_error(error_r, translate_quark(), 0,
                    "malformed %s packet", name);
        return false;
    }

    return true;
}

static bool
translate_client_pair(struct param_array &array, const char *name,
                      const char *payload, size_t payload_length,
                      GError **error_r)
{
    if (array.IsFull()) {
        g_set_error(error_r, translate_quark(), 0,
                    "too many %s packets", name);
        return false;
    }

    if (!translate_client_check_pair(name, payload, payload_length, error_r))
        return false;

    array.Append(payload);
    return true;
}

static bool
translate_client_expand_pair(struct param_array &array, const char *name,
                             const char *payload, size_t payload_length,
                             GError **error_r)
{
    if (!array.CanSetExpand()) {
        g_set_error(error_r, translate_quark(), 0,
                    "misplaced %s packet", name);
        return false;
    }

    if (!translate_client_check_pair(name, payload, payload_length, error_r))
        return false;

    array.SetExpand(payload);
    return true;
}

static bool
translate_client_pivot_root(NamespaceOptions *ns,
                            const char *payload, size_t payload_length,
                            GError **error_r)
{
    if (!is_valid_absolute_path(payload, payload_length)) {
        g_set_error_literal(error_r, translate_quark(), 0,
                            "malformed PIVOT_ROOT packet");
        return false;
    }

    if (ns == nullptr || ns->pivot_root != nullptr) {
        g_set_error_literal(error_r, translate_quark(), 0,
                            "misplaced PIVOT_ROOT packet");
        return false;
    }

    ns->enable_mount = true;
    ns->pivot_root = payload;
    return true;
}

static bool
translate_client_home(NamespaceOptions *ns, JailParams *jail,
                      const char *payload, size_t payload_length,
                      GError **error_r)
{
    if (!is_valid_absolute_path(payload, payload_length)) {
        g_set_error_literal(error_r, translate_quark(), 0,
                            "malformed HOME packet");
        return false;
    }

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
        g_set_error_literal(error_r, translate_quark(), 0,
                            "misplaced HOME packet");

    return ok;
}

static bool
translate_client_expand_home(NamespaceOptions *ns, JailParams *jail,
                             const char *payload, size_t payload_length,
                             GError **error_r)
{
    if (!is_valid_absolute_path(payload, payload_length)) {
        g_set_error_literal(error_r, translate_quark(), 0,
                            "malformed EXPAND_HOME packet");
        return false;
    }

    bool ok = false;

    if (ns != nullptr && ns->expand_home == nullptr) {
        ns->expand_home = payload;
        ok = true;
    }

    if (jail != nullptr && jail->enabled &&
        jail->expand_home_directory == nullptr) {
        jail->expand_home_directory = payload;
        ok = true;
    }

    if (!ok)
        g_set_error_literal(error_r, translate_quark(), 0,
                            "misplaced EXPAND_HOME packet");

    return ok;
}

static bool
translate_client_mount_proc(NamespaceOptions *ns,
                            size_t payload_length,
                            GError **error_r)
{
    if (payload_length > 0) {
        g_set_error_literal(error_r, translate_quark(), 0,
                            "malformed MOUNT_PROC packet");
        return false;
    }

    if (ns == nullptr || ns->mount_proc) {
        g_set_error_literal(error_r, translate_quark(), 0,
                            "misplaced MOUNT_PROC packet");
        return false;
    }

    ns->enable_mount = true;
    ns->mount_proc = true;
    return true;
}

static bool
translate_client_mount_tmp_tmpfs(NamespaceOptions *ns,
                                 ConstBuffer<char> payload,
                                 GError **error_r)
{
    if (has_null_byte(payload.data, payload.size)) {
        g_set_error_literal(error_r, translate_quark(), 0,
                            "malformed MOUNT_TMP_TMPFS packet");
        return false;
    }

    if (ns == nullptr || ns->mount_tmp_tmpfs != nullptr) {
        g_set_error_literal(error_r, translate_quark(), 0,
                            "misplaced MOUNT_TMP_TMPFS packet");
        return false;
    }

    ns->enable_mount = true;
    ns->mount_tmp_tmpfs = payload.data != nullptr
        ? payload.data
        : "";
    return true;
}

static bool
translate_client_mount_home(NamespaceOptions *ns,
                            const char *payload, size_t payload_length,
                            GError **error_r)
{
    if (!is_valid_absolute_path(payload, payload_length)) {
        g_set_error_literal(error_r, translate_quark(), 0,
                            "malformed MOUNT_HOME packet");
        return false;
    }

    if (ns == nullptr || ns->home == nullptr ||
        ns->mount_home != nullptr) {
        g_set_error_literal(error_r, translate_quark(), 0,
                            "misplaced MOUNT_HOME packet");
        return false;
    }

    ns->enable_mount = true;
    ns->mount_home = payload;
    return true;
}

static bool
translate_client_mount_tmpfs(NamespaceOptions *ns,
                            const char *payload, size_t payload_length,
                            GError **error_r)
{
    if (!is_valid_absolute_path(payload, payload_length) ||
        /* not allowed for /tmp, use TRANSLATE_MOUNT_TMP_TMPFS
           instead! */
        strcmp(payload, "/tmp") == 0) {
        g_set_error_literal(error_r, translate_quark(), 0,
                            "malformed MOUNT_TMPFS packet");
        return false;
    }

    if (ns == nullptr || ns->mount_tmpfs != nullptr) {
        g_set_error_literal(error_r, translate_quark(), 0,
                            "misplaced MOUNT_TMPFS packet");
        return false;
    }

    ns->enable_mount = true;
    ns->mount_tmpfs = payload;
    return true;
}

inline bool
TranslateParser::HandleBindMount(const char *payload, size_t payload_length,
                                 bool expand, bool writable, GError **error_r)
{
    if (*payload != '/') {
        g_set_error_literal(error_r, translate_quark(), 0,
                            "malformed BIND_MOUNT packet");
        return false;
    }

    const char *separator = (const char *)memchr(payload, 0, payload_length);
    if (separator == nullptr || separator[1] != '/') {
        g_set_error_literal(error_r, translate_quark(), 0,
                            "malformed BIND_MOUNT packet");
        return false;
    }

    if (mount_list == nullptr) {
        g_set_error_literal(error_r, translate_quark(), 0,
                            "misplaced BIND_MOUNT packet");
        return false;
    }

    auto *m = NewFromPool<MountList>(*pool,
                                     /* skip the slash to make it relative */
                                     payload + 1,
                                     separator + 1,
                                     expand, writable);
    *mount_list = m;
    mount_list = &m->next;
    return true;
}

static bool
translate_client_uts_namespace(NamespaceOptions *ns,
                               const char *payload,
                               GError **error_r)
{
    if (*payload == 0) {
        g_set_error_literal(error_r, translate_quark(), 0,
                            "malformed MOUNT_UTS_NAMESPACE packet");
        return false;
    }

    if (ns == nullptr || ns->hostname != nullptr) {
        g_set_error_literal(error_r, translate_quark(), 0,
                            "misplaced MOUNT_UTS_NAMESPACE packet");
        return false;
    }

    ns->hostname = payload;
    return true;
}

static bool
translate_client_rlimits(ChildOptions *child_options,
                         const char *payload,
                         GError **error_r)
{
    if (child_options == nullptr) {
        g_set_error_literal(error_r, translate_quark(), 0,
                            "misplaced RLIMITS packet");
        return false;
    }

    if (!child_options->rlimits.Parse(payload)) {
        g_set_error_literal(error_r, translate_quark(), 0,
                            "malformed RLIMITS packet");
        return false;
    }

    return true;
}

inline bool
TranslateParser::HandleWant(const uint16_t *payload, size_t payload_length,
                            GError **error_r)
{
    if (response.protocol_version < 1) {
        g_set_error_literal(error_r, translate_quark(), 0,
                            "WANT requires protocol version 1");
        return false;
    }

    if (from_request.want) {
        g_set_error_literal(error_r, translate_quark(), 0,
                            "WANT loop");
        return false;
    }

    if (!response.want.IsEmpty()) {
        g_set_error_literal(error_r, translate_quark(), 0,
                            "duplicate WANT packet");
        return false;
    }

    if (payload_length % sizeof(payload[0]) != 0) {
        g_set_error_literal(error_r, translate_quark(), 0,
                            "malformed WANT packet");
        return false;
    }

    response.want = { payload, payload_length / sizeof(payload[0]) };
    return true;
}

static bool
translate_client_file_not_found(TranslateResponse &response,
                                ConstBuffer<void> payload,
                                GError **error_r)
{
    if (!response.file_not_found.IsNull()) {
        g_set_error_literal(error_r, translate_quark(), 0,
                            "duplicate FILE_NOT_FOUND packet");
        return false;
    }

    if (response.test_path == nullptr &&
        response.expand_test_path == nullptr) {
        switch (response.address.type) {
        case ResourceAddress::Type::NONE:
            g_set_error_literal(error_r, translate_quark(), 0,
                                "FIlE_NOT_FOUND without resource address");
            return false;

        case ResourceAddress::Type::HTTP:
        case ResourceAddress::Type::PIPE:
            g_set_error_literal(error_r, translate_quark(), 0,
                                "FIlE_NOT_FOUND not compatible with resource address");
            return false;

        case ResourceAddress::Type::LOCAL:
        case ResourceAddress::Type::NFS:
        case ResourceAddress::Type::CGI:
        case ResourceAddress::Type::FASTCGI:
        case ResourceAddress::Type::WAS:
        case ResourceAddress::Type::LHTTP:
            break;
        }
    }

    response.file_not_found = payload;
    return true;
}

inline bool
TranslateParser::HandleContentTypeLookup(ConstBuffer<void> payload,
                                         GError **error_r)
{
    const char *content_type;
    ConstBuffer<void> *content_type_lookup;

    if (file_address != nullptr) {
        content_type = file_address->content_type;
        content_type_lookup = &file_address->content_type_lookup;
    } else if (nfs_address != nullptr) {
        content_type = nfs_address->content_type;
        content_type_lookup = &nfs_address->content_type_lookup;
    } else {
        g_set_error_literal(error_r, translate_quark(), 0,
                            "misplaced CONTENT_TYPE_LOOKUP");
        return false;
    }

    if (!content_type_lookup->IsNull()) {
        g_set_error_literal(error_r, translate_quark(), 0,
                            "duplicate CONTENT_TYPE_LOOKUP");
        return false;
    }

    if (content_type != nullptr) {
        g_set_error_literal(error_r, translate_quark(), 0,
                            "CONTENT_TYPE/CONTENT_TYPE_LOOKUP conflict");
        return false;
    }

    *content_type_lookup = payload;
    return true;
}

static bool
translate_client_enotdir(TranslateResponse &response,
                         ConstBuffer<void> payload,
                         GError **error_r)
{
    if (!response.enotdir.IsNull()) {
        g_set_error_literal(error_r, translate_quark(), 0,
                            "duplicate ENOTDIR");
        return false;
    }

    if (response.test_path == nullptr) {
        switch (response.address.type) {
        case ResourceAddress::Type::NONE:
        g_set_error_literal(error_r, translate_quark(), 0,
                            "ENOTDIR without resource address");
            return false;

        case ResourceAddress::Type::HTTP:
        case ResourceAddress::Type::PIPE:
        case ResourceAddress::Type::NFS:
            g_set_error_literal(error_r, translate_quark(), 0,
                                "ENOTDIR not compatible with resource address");
            return false;

        case ResourceAddress::Type::LOCAL:
        case ResourceAddress::Type::CGI:
        case ResourceAddress::Type::FASTCGI:
        case ResourceAddress::Type::WAS:
        case ResourceAddress::Type::LHTTP:
            break;
        }
    }

    response.enotdir = payload;
    return true;
}

static bool
translate_client_directory_index(TranslateResponse &response,
                                 ConstBuffer<void> payload,
                                 GError **error_r)
{
    if (!response.directory_index.IsNull()) {
        g_set_error_literal(error_r, translate_quark(), 0,
                            "duplicate DIRECTORY_INDEX");
        return false;
    }

    if (response.test_path == nullptr &&
        response.expand_test_path == nullptr) {
        switch (response.address.type) {
        case ResourceAddress::Type::NONE:
        g_set_error_literal(error_r, translate_quark(), 0,
                            "DIRECTORY_INDEX without resource address");
            return false;

        case ResourceAddress::Type::HTTP:
        case ResourceAddress::Type::LHTTP:
        case ResourceAddress::Type::PIPE:
        case ResourceAddress::Type::CGI:
        case ResourceAddress::Type::FASTCGI:
        case ResourceAddress::Type::WAS:
        g_set_error_literal(error_r, translate_quark(), 0,
                            "DIRECTORY_INDEX not compatible with resource address");
            return false;

        case ResourceAddress::Type::LOCAL:
        case ResourceAddress::Type::NFS:
            break;
        }
    }

    response.directory_index = payload;
    return true;
}

static bool
translate_client_expires_relative(TranslateResponse &response,
                                  ConstBuffer<void> payload,
                                  GError **error_r)
{
    if (response.expires_relative > std::chrono::seconds::zero()) {
        g_set_error_literal(error_r, translate_quark(), 0,
                            "duplicate EXPIRES_RELATIVE");
        return false;
    }

    if (payload.size != sizeof(uint32_t)) {
        g_set_error_literal(error_r, translate_quark(), 0,
                            "malformed EXPIRES_RELATIVE");
        return false;
    }

    response.expires_relative = std::chrono::seconds(*(const uint32_t *)payload.data);
    return true;
}

static bool
translate_client_stderr_path(ChildOptions *child_options,
                             ConstBuffer<void> payload,
                             GError **error_r)
{
    const char *path = (const char *)payload.data;
    if (!is_valid_absolute_path(path, payload.size)) {
        g_set_error_literal(error_r, translate_quark(), 0,
                            "malformed STDERR_PATH packet");
        return false;
    }

    if (child_options == nullptr) {
        g_set_error_literal(error_r, translate_quark(), 0,
                            "misplaced STDERR_PATH packet");
        return false;
    }

    if (child_options->stderr_path != nullptr) {
        g_set_error_literal(error_r, translate_quark(), 0,
                            "duplicate STDERR_PATH packet");
        return false;
    }

    child_options->stderr_path = path;
    return true;
}

static bool
translate_client_expand_stderr_path(ChildOptions *child_options,
                                    ConstBuffer<void> payload,
                                    GError **error_r)
{
    const char *path = (const char *)payload.data;
    if (!is_valid_nonempty_string((const char *)payload.data, payload.size)) {
        g_set_error_literal(error_r, translate_quark(), 0,
                            "malformed EXPAND_STDERR_PATH packet");
        return false;
    }

    if (child_options == nullptr) {
        g_set_error_literal(error_r, translate_quark(), 0,
                            "misplaced EXPAND_STDERR_PATH packet");
        return false;
    }

    if (child_options->expand_stderr_path != nullptr) {
        g_set_error_literal(error_r, translate_quark(), 0,
                            "duplicate EXPAND_STDERR_PATH packet");
        return false;
    }

    child_options->expand_stderr_path = path;
    return true;
}

gcc_pure
static bool
CheckRefence(StringView payload)
{
    auto p = payload.begin();
    const auto end = payload.end();

    while (true) {
        auto n = std::find(p, end, '\0');
        if (n == p)
            return false;

        if (n == end)
            return true;

        p = n + 1;
    }
}

inline bool
TranslateParser::HandleRefence(StringView payload,
                               GError **error_r)
{
    if (child_options == nullptr || !child_options->refence.IsEmpty()) {
        g_set_error_literal(error_r, translate_quark(), 0,
                            "misplaced REFENCE packet");
        return false;
    }

    if (!CheckRefence(payload)) {
        g_set_error_literal(error_r, translate_quark(), 0,
                            "malformed REFENCE packet");
        return false;
    }

    child_options->refence.Set(payload);
    return true;
}

inline bool
TranslateParser::HandleUidGid(ConstBuffer<void> _payload,
                              GError **error_r)
{
    if (child_options == nullptr || !child_options->uid_gid.IsEmpty()) {
        g_set_error_literal(error_r, translate_quark(), 0,
                            "misplaced UID_GID packet");
        return false;
    }

    UidGid &uid_gid = child_options->uid_gid;

    constexpr size_t min_size = sizeof(int) * 2;
    const size_t max_size = min_size + sizeof(int) * uid_gid.groups.max_size();

    if (_payload.size < min_size || _payload.size > max_size ||
        _payload.size % sizeof(int) != 0) {
        g_set_error_literal(error_r, translate_quark(), 0,
                            "malformed UID_GID packet");
        return false;
    }

    const auto payload = ConstBuffer<int>::FromVoid(_payload);
    uid_gid.uid = payload[0];
    uid_gid.gid = payload[1];

    size_t n_groups = payload.size - 2;
    std::copy(std::next(payload.begin(), 2), payload.end(),
              uid_gid.groups.begin());
    if (n_groups < uid_gid.groups.max_size())
        uid_gid.groups[n_groups] = 0;

    return true;
}

gcc_pure
static bool
IsValidCgroupSetName(StringView name)
{
    const char *dot = name.Find('.');
    if (dot == nullptr || dot == name.begin() || dot == name.end())
        return false;

    const StringView controller(name.data, dot);

    for (char ch : controller)
        if (!IsLowerAlphaASCII(ch))
            return false;

    if (controller.Equals("cgroup"))
        /* this is not a controller, this is a core cgroup
           attribute */
        return false;

    const StringView attribute(dot + 1, name.end());

    for (char ch : controller)
        if (!IsLowerAlphaASCII(ch) && ch != '.' && ch != '_')
            return false;

    return true;
}

gcc_pure
static bool
IsValidCgroupSetValue(StringView value)
{
    return !value.IsEmpty() && value.Find('/') == nullptr;
}

gcc_pure
static std::pair<StringView, StringView>
ParseCgroupSet(StringView payload)
{
    if (has_null_byte(payload.data, payload.size))
        return std::make_pair(nullptr, nullptr);

    const char *eq = payload.Find('=');
    if (eq == nullptr)
        return std::make_pair(nullptr, nullptr);

    StringView name(payload.data, eq), value(eq + 1, payload.end());
    if (!IsValidCgroupSetName(name) || !IsValidCgroupSetValue(value))
        return std::make_pair(nullptr, nullptr);

    return std::make_pair(name, value);
}

inline bool
TranslateParser::HandleCgroupSet(StringView payload, GError **error_r)
{
    if (child_options == nullptr) {
        g_set_error_literal(error_r, translate_quark(), 0,
                            "misplaced CGROUP_SET packet");
        return false;
    }

    auto set = ParseCgroupSet(payload);
    if (set.first.IsNull()) {
        g_set_error_literal(error_r, translate_quark(), 0,
                            "malformed CGROUP_SET packet");
        return false;
    }

    child_options->cgroup.Set(*pool, set.first, set.second);
    return true;
}

static bool
CheckProbeSuffix(const char *payload, size_t length)
{
    return memchr(payload, '/', length) == nullptr &&
        !has_null_byte(payload, length);
}

inline bool
TranslateParser::HandleRegularPacket(enum beng_translation_command command,
                                     const void *const _payload, size_t payload_length,
                                     GError **error_r)
{
    const char *const payload = (const char *)_payload;

    switch (command) {
        Transformation *new_transformation;

    case TRANSLATE_BEGIN:
    case TRANSLATE_END:
        gcc_unreachable();

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
    case TRANSLATE_LOGIN:
    case TRANSLATE_PASSWORD:
    case TRANSLATE_SERVICE:
        g_set_error_literal(error_r, translate_quark(), 0,
                            "misplaced translate request packet");
        return false;

    case TRANSLATE_UID_GID:
        return HandleUidGid({_payload, payload_length}, error_r);

    case TRANSLATE_STATUS:
        if (payload_length != 2) {
            g_set_error_literal(error_r, translate_quark(), 0,
                                "size mismatch in STATUS packet from translation server");
            return false;
        }

        response.status = http_status_t(*(const uint16_t*)(const void *)payload);

        if (!http_status_is_valid(response.status)) {
            g_set_error(error_r, translate_quark(), 0,
                        "invalid HTTP status code %u", response.status);
            return false;
        }

        return true;

    case TRANSLATE_PATH:
        if (!is_valid_absolute_path(payload, payload_length)) {
            g_set_error_literal(error_r, translate_quark(), 0,
                                "malformed PATH packet");
            return false;
        }

        if (nfs_address != nullptr && *nfs_address->path == 0) {
            nfs_address->path = payload;
            return true;
        }

        if (resource_address == nullptr ||
            resource_address->type != ResourceAddress::Type::NONE) {
            g_set_error_literal(error_r, translate_quark(), 0,
                                "misplaced PATH packet");
            return false;
        }

        file_address = file_address_new(*pool, payload);
        *resource_address = *file_address;
        return true;

    case TRANSLATE_PATH_INFO:
        if (has_null_byte(payload, payload_length)) {
            g_set_error_literal(error_r, translate_quark(), 0,
                                "malformed PATH_INFO packet");
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
            g_set_error_literal(error_r, translate_quark(), 0,
                                "misplaced PATH_INFO packet");
            return false;
        }

    case TRANSLATE_EXPAND_PATH:
        if (has_null_byte(payload, payload_length)) {
            g_set_error_literal(error_r, translate_quark(), 0,
                                "malformed EXPAND_PATH packet");
            return false;
        }

        if (response.regex == nullptr) {
            g_set_error_literal(error_r, translate_quark(), 0,
                                "misplaced EXPAND_PATH packet");
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
            g_set_error_literal(error_r, translate_quark(), 0,
                                "misplaced EXPAND_PATH packet");
            return false;
        }

    case TRANSLATE_EXPAND_PATH_INFO:
        if (has_null_byte(payload, payload_length)) {
            g_set_error_literal(error_r, translate_quark(), 0,
                                "malformed EXPAND_PATH_INFO packet");
            return false;
        }

        if (response.regex == nullptr) {
            g_set_error_literal(error_r, translate_quark(), 0,
                                "misplaced EXPAND_PATH_INFO packet");
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
            g_set_error_literal(error_r, translate_quark(), 0,
                                "misplaced EXPAND_PATH_INFO packet");
            return false;
        }

    case TRANSLATE_DEFLATED:
        if (!is_valid_absolute_path(payload, payload_length)) {
            g_set_error_literal(error_r, translate_quark(), 0,
                                "malformed DEFLATED packet");
            return false;
        }

        if (file_address != nullptr) {
            file_address->deflated = payload;
            return true;
        } else if (nfs_address != nullptr) {
            /* ignore for now */
        } else {
            g_set_error_literal(error_r, translate_quark(), 0,
                                "misplaced DEFLATED packet");
            return false;
        }

    case TRANSLATE_GZIPPED:
        if (!is_valid_absolute_path(payload, payload_length)) {
            g_set_error_literal(error_r, translate_quark(), 0,
                                "malformed GZIPPED packet");
            return false;
        }

        if (file_address != nullptr) {
            if (file_address->auto_gzipped ||
                file_address->gzipped != nullptr) {
                g_set_error_literal(error_r, translate_quark(), 0,
                                    "misplaced GZIPPED packet");
                return false;
            }

            file_address->gzipped = payload;
            return true;
        } else if (nfs_address != nullptr) {
            /* ignore for now */
        } else {
            g_set_error_literal(error_r, translate_quark(), 0,
                                "misplaced GZIPPED packet");
            return false;
        }

    case TRANSLATE_SITE:
        assert(resource_address != nullptr);

        if (!is_valid_nonempty_string(payload, payload_length)) {
            g_set_error_literal(error_r, translate_quark(), 0,
                                "malformed SITE packet");
            return false;
        }

        if (resource_address == &response.address)
            response.site = payload;
        else if (jail != nullptr && jail->enabled)
            jail->site_id = payload;
        else {
            g_set_error_literal(error_r, translate_quark(), 0,
                                "misplaced SITE packet");
            return false;
        }

        return true;

    case TRANSLATE_CONTENT_TYPE:
        if (!is_valid_nonempty_string(payload, payload_length)) {
            g_set_error_literal(error_r, translate_quark(), 0,
                                "malformed CONTENT_TYPE packet");
            return false;
        }

        if (file_address != nullptr) {
            if (!file_address->content_type_lookup.IsNull()) {
                g_set_error_literal(error_r, translate_quark(), 0,
                                    "CONTENT_TYPE/CONTENT_TYPE_LOOKUP conflict");
                return false;
            }

            file_address->content_type = payload;
            return true;
        } else if (nfs_address != nullptr) {
            if (!nfs_address->content_type_lookup.IsNull()) {
                g_set_error_literal(error_r, translate_quark(), 0,
                                    "CONTENT_TYPE/CONTENT_TYPE_LOOKUP conflict");
                return false;
            }

            nfs_address->content_type = payload;
            return true;
        } else if (from_request.content_type_lookup) {
            response.content_type = payload;
            return true;
        } else {
            g_set_error_literal(error_r, translate_quark(), 0,
                                "misplaced CONTENT_TYPE packet");
            return false;
        }

    case TRANSLATE_HTTP:
        if (resource_address == nullptr ||
            resource_address->type != ResourceAddress::Type::NONE) {
            g_set_error_literal(error_r, translate_quark(), 0,
                                "misplaced HTTP packet");
            return false;
        }

        if (!is_valid_nonempty_string(payload, payload_length)) {
            g_set_error_literal(error_r, translate_quark(), 0,
                                "malformed HTTP packet");
            return false;
        }

        http_address = http_address_parse(pool, payload, error_r);
        if (http_address == nullptr)
            return false;

        if (http_address->protocol != HttpAddress::Protocol::HTTP) {
            g_set_error_literal(error_r, translate_quark(), 0,
                                "malformed HTTP packet");
            return false;
        }

        *resource_address = *http_address;

        address_list = &http_address->addresses;
        default_port = http_address->GetDefaultPort();
        return true;

    case TRANSLATE_REDIRECT:
        if (!is_valid_nonempty_string(payload, payload_length)) {
            g_set_error_literal(error_r, translate_quark(), 0,
                                "malformed REDIRECT packet");
            return false;
        }

        response.redirect = payload;
        return true;

    case TRANSLATE_EXPAND_REDIRECT:
        if (response.regex == nullptr ||
            response.redirect == nullptr ||
            response.expand_redirect != nullptr) {
            g_set_error_literal(error_r, translate_quark(), 0,
                                "misplaced EXPAND_REDIRECT packet");
            return false;
        }

        if (!is_valid_nonempty_string(payload, payload_length)) {
            g_set_error_literal(error_r, translate_quark(), 0,
                                "malformed EXPAND_REDIRECT packet");
            return false;
        }

        response.expand_redirect = payload;
        return true;

    case TRANSLATE_BOUNCE:
        if (!is_valid_nonempty_string(payload, payload_length)) {
            g_set_error_literal(error_r, translate_quark(), 0,
                                "malformed BOUNCE packet");
            return false;
        }

        response.bounce = payload;
        return true;

    case TRANSLATE_FILTER:
        new_transformation = AddTransformation();
        new_transformation->type = Transformation::Type::FILTER;
        new_transformation->u.filter.address.type = ResourceAddress::Type::NONE;
        new_transformation->u.filter.reveal_user = false;
        resource_address = &new_transformation->u.filter.address;
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
        new_transformation = AddTransformation();
        new_transformation->type = Transformation::Type::PROCESS;
        new_transformation->u.processor.options = PROCESSOR_REWRITE_URL;
        return true;

    case TRANSLATE_DOMAIN:
        daemon_log(2, "deprecated DOMAIN packet\n");
        return true;

    case TRANSLATE_CONTAINER:
        if (transformation == nullptr ||
            transformation->type != Transformation::Type::PROCESS) {
            g_set_error_literal(error_r, translate_quark(), 0,
                                "misplaced CONTAINER packet");
            return false;
        }

        transformation->u.processor.options |= PROCESSOR_CONTAINER;
        return true;

    case TRANSLATE_SELF_CONTAINER:
        if (transformation == nullptr ||
            transformation->type != Transformation::Type::PROCESS) {
            g_set_error_literal(error_r, translate_quark(), 0,
                                "misplaced SELF_CONTAINER packet");
            return false;
        }

        transformation->u.processor.options |=
            PROCESSOR_SELF_CONTAINER|PROCESSOR_CONTAINER;
        return true;

    case TRANSLATE_GROUP_CONTAINER:
        if (!is_valid_nonempty_string(payload, payload_length)) {
            g_set_error_literal(error_r, translate_quark(), 0,
                                "malformed GROUP_CONTAINER packet");
            return false;
        }

        if (transformation == nullptr ||
            transformation->type != Transformation::Type::PROCESS) {
            g_set_error_literal(error_r, translate_quark(), 0,
                                "misplaced GROUP_CONTAINER packet");
            return false;
        }

        transformation->u.processor.options |= PROCESSOR_CONTAINER;
        response.container_groups.Add(*pool, payload);
        return true;

    case TRANSLATE_WIDGET_GROUP:
        if (!is_valid_nonempty_string(payload, payload_length)) {
            g_set_error_literal(error_r, translate_quark(), 0,
                                "malformed WIDGET_GROUP packet");
            return false;
        }

        response.widget_group = payload;
        return true;

    case TRANSLATE_UNTRUSTED:
        if (!is_valid_nonempty_string(payload, payload_length) || *payload == '.' ||
            payload[payload_length - 1] == '.') {
            g_set_error_literal(error_r, translate_quark(), 0,
                                "malformed UNTRUSTED packet");
            return false;
        }

        if (response.HasUntrusted()) {
            g_set_error_literal(error_r, translate_quark(), 0,
                                "misplaced UNTRUSTED packet");
            return false;
        }

        response.untrusted = payload;
        return true;

    case TRANSLATE_UNTRUSTED_PREFIX:
        if (!is_valid_nonempty_string(payload, payload_length) || *payload == '.' ||
            payload[payload_length - 1] == '.') {
            g_set_error_literal(error_r, translate_quark(), 0,
                                "malformed UNTRUSTED_PREFIX packet");
            return false;
        }

        if (response.HasUntrusted()) {
            g_set_error_literal(error_r, translate_quark(), 0,
                                "misplaced UNTRUSTED_PREFIX packet");
            return false;
        }

        response.untrusted_prefix = payload;
        return true;

    case TRANSLATE_UNTRUSTED_SITE_SUFFIX:
        if (!is_valid_nonempty_string(payload, payload_length) || *payload == '.' ||
            payload[payload_length - 1] == '.') {
            g_set_error_literal(error_r, translate_quark(), 0,
                                "malformed UNTRUSTED_SITE_SUFFIX packet");
            return false;
        }

        if (response.HasUntrusted()) {
            g_set_error_literal(error_r, translate_quark(), 0,
                                "misplaced UNTRUSTED_SITE_SUFFIX packet");
            return false;
        }

        response.untrusted_site_suffix = payload;
        return true;

    case TRANSLATE_SCHEME:
        if (strncmp(payload, "http", 4) != 0) {
            g_set_error_literal(error_r, translate_quark(), 0,
                                "misplaced SCHEME packet");
            return false;
        }

        response.scheme = payload;
        return true;

    case TRANSLATE_HOST:
        response.host = payload;
        return true;

    case TRANSLATE_URI:
        if (!is_valid_absolute_uri(payload, payload_length)) {
            g_set_error_literal(error_r, translate_quark(), 0,
                                "malformed URI packet");
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
        if (payload_length > 0) {
            g_set_error_literal(error_r, translate_quark(), 0,
                                "malformed REALM packet");
            return false;
        }

        if (response.realm != nullptr) {
            g_set_error_literal(error_r, translate_quark(), 0,
                                "duplicate REALM packet");
            return false;
        }

        if (response.realm_from_auth_base) {
            g_set_error_literal(error_r, translate_quark(), 0,
                                "misplaced REALM packet");
            return false;
        }

        response.realm = payload;
        return true;

    case TRANSLATE_LANGUAGE:
        response.language = payload;
        return true;

    case TRANSLATE_PIPE:
        if (resource_address == nullptr ||
            resource_address->type != ResourceAddress::Type::NONE) {
            g_set_error_literal(error_r, translate_quark(), 0,
                                "misplaced PIPE packet");
            return false;
        }

        if (payload_length == 0) {
            g_set_error_literal(error_r, translate_quark(), 0,
                                "malformed PIPE packet");
            return false;
        }

        cgi_address = cgi_address_new(*pool, payload);
        *resource_address = ResourceAddress(ResourceAddress::Type::PIPE,
                                           *cgi_address);

        child_options = &cgi_address->options;
        ns_options = &child_options->ns;
        mount_list = &ns_options->mounts;
        jail = &child_options->jail;
        return true;

    case TRANSLATE_CGI:
        if (resource_address == nullptr ||
            resource_address->type != ResourceAddress::Type::NONE) {
            g_set_error_literal(error_r, translate_quark(), 0,
                                "misplaced CGI packet");
            return false;
        }

        if (!is_valid_absolute_path(payload, payload_length)) {
            g_set_error_literal(error_r, translate_quark(), 0,
                                "malformed CGI packet");
            return false;
        }

        cgi_address = cgi_address_new(*pool, payload);
        *resource_address = ResourceAddress(ResourceAddress::Type::CGI,
                                           *cgi_address);

        cgi_address->document_root = response.document_root;
        child_options = &cgi_address->options;
        ns_options = &child_options->ns;
        mount_list = &ns_options->mounts;
        jail = &child_options->jail;
        return true;

    case TRANSLATE_FASTCGI:
        if (resource_address == nullptr ||
            resource_address->type != ResourceAddress::Type::NONE) {
            g_set_error_literal(error_r, translate_quark(), 0,
                                "misplaced FASTCGI packet");
            return false;
        }

        if (!is_valid_absolute_path(payload, payload_length)) {
            g_set_error_literal(error_r, translate_quark(), 0,
                                "malformed FASTCGI packet");
            return false;
        }

        cgi_address = cgi_address_new(*pool, payload);
        *resource_address = ResourceAddress(ResourceAddress::Type::FASTCGI,
                                           *cgi_address);

        child_options = &cgi_address->options;
        ns_options = &child_options->ns;
        mount_list = &ns_options->mounts;
        jail = &child_options->jail;
        address_list = &cgi_address->address_list;
        default_port = 9000;
        return true;

    case TRANSLATE_AJP:
        if (resource_address == nullptr ||
            resource_address->type != ResourceAddress::Type::NONE) {
            g_set_error_literal(error_r, translate_quark(), 0,
                                "misplaced AJP packet");
            return false;
        }

        if (payload_length == 0) {
            g_set_error_literal(error_r, translate_quark(), 0,
                                "malformed AJP packet");
            return false;
        }

        http_address = http_address_parse(pool, payload, error_r);
        if (http_address == nullptr)
            return false;

        if (http_address->protocol != HttpAddress::Protocol::AJP) {
            g_set_error_literal(error_r, translate_quark(), 0,
                                "malformed AJP packet");
            return false;
        }

        *resource_address = *http_address;

        address_list = &http_address->addresses;
        default_port = 8009;
        return true;

    case TRANSLATE_NFS_SERVER:
        if (resource_address == nullptr ||
            resource_address->type != ResourceAddress::Type::NONE) {
            g_set_error_literal(error_r, translate_quark(), 0,
                                "misplaced NFS_SERVER packet");
            return false;
        }

        if (payload_length == 0) {
            g_set_error_literal(error_r, translate_quark(), 0,
                                "malformed NFS_SERVER packet");
            return false;
        }

        nfs_address = nfs_address_new(*pool, payload, "", "");
        *resource_address = *nfs_address;
        return true;

    case TRANSLATE_NFS_EXPORT:
        if (nfs_address == nullptr ||
            *nfs_address->export_name != 0) {
            g_set_error_literal(error_r, translate_quark(), 0,
                                "misplaced NFS_EXPORT packet");
            return false;
        }

        if (!is_valid_absolute_path(payload, payload_length)) {
            g_set_error_literal(error_r, translate_quark(), 0,
                                "malformed NFS_EXPORT packet");
            return false;
        }

        nfs_address->export_name = payload;
        return true;

    case TRANSLATE_JAILCGI:
        if (jail == nullptr) {
            g_set_error_literal(error_r, translate_quark(), 0,
                                "misplaced JAILCGI packet");
            return false;
        }

        jail->enabled = true;
        return true;

    case TRANSLATE_HOME:
        return translate_client_home(ns_options, jail, payload, payload_length,
                                     error_r);

    case TRANSLATE_INTERPRETER:
        if (resource_address == nullptr ||
            (resource_address->type != ResourceAddress::Type::CGI &&
             resource_address->type != ResourceAddress::Type::FASTCGI) ||
            cgi_address->interpreter != nullptr) {
            g_set_error_literal(error_r, translate_quark(), 0,
                                "misplaced INTERPRETER packet");
            return false;
        }

        cgi_address->interpreter = payload;
        return true;

    case TRANSLATE_ACTION:
        if (resource_address == nullptr ||
            (resource_address->type != ResourceAddress::Type::CGI &&
             resource_address->type != ResourceAddress::Type::FASTCGI) ||
            cgi_address->action != nullptr) {
            g_set_error_literal(error_r, translate_quark(), 0,
                                "misplaced ACTION packet");
            return false;
        }

        cgi_address->action = payload;
        return true;

    case TRANSLATE_SCRIPT_NAME:
        if (resource_address == nullptr ||
            (resource_address->type != ResourceAddress::Type::CGI &&
             resource_address->type != ResourceAddress::Type::WAS &&
             resource_address->type != ResourceAddress::Type::FASTCGI) ||
            cgi_address->script_name != nullptr) {
            g_set_error_literal(error_r, translate_quark(), 0,
                                "misplaced SCRIPT_NAME packet");
            return false;
        }

        cgi_address->script_name = payload;
        return true;

    case TRANSLATE_EXPAND_SCRIPT_NAME:
        if (!is_valid_nonempty_string(payload, payload_length)) {
            g_set_error_literal(error_r, translate_quark(), 0,
                                "malformed EXPAND_SCRIPT_NAME packet");
            return false;
        }

        if (response.regex == nullptr ||
            cgi_address == nullptr ||
            cgi_address->expand_script_name != nullptr) {
            g_set_error_literal(error_r, translate_quark(), 0,
                                "misplaced EXPAND_SCRIPT_NAME packet");
            return false;
        }

        cgi_address->expand_script_name = payload;
        return true;

    case TRANSLATE_DOCUMENT_ROOT:
        if (!is_valid_absolute_path(payload, payload_length)) {
            g_set_error_literal(error_r, translate_quark(), 0,
                                "malformed DOCUMENT_ROOT packet");
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
            g_set_error_literal(error_r, translate_quark(), 0,
                                "malformed EXPAND_DOCUMENT_ROOT packet");
            return false;
        }

        if (response.regex == nullptr) {
            g_set_error_literal(error_r, translate_quark(), 0,
                                "misplaced EXPAND_DOCUMENT_ROOT packet");
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
            g_set_error_literal(error_r, translate_quark(), 0,
                                "misplaced ADDRESS packet");
            return false;
        }

        if (payload_length < 2) {
            g_set_error_literal(error_r, translate_quark(), 0,
                                "malformed INTERPRETER packet");
            return false;
        }

        address_list->Add(pool,
                          SocketAddress((const struct sockaddr *)_payload,
                                        payload_length));
        return true;

    case TRANSLATE_ADDRESS_STRING:
        if (address_list == nullptr) {
            g_set_error_literal(error_r, translate_quark(), 0,
                                "misplaced ADDRESS_STRING packet");
            return false;
        }

        if (payload_length == 0) {
            g_set_error_literal(error_r, translate_quark(), 0,
                                "malformed ADDRESS_STRING packet");
            return false;
        }

        {
            bool ret;

            ret = parse_address_string(pool, address_list,
                                       payload, default_port);
            if (!ret) {
                g_set_error_literal(error_r, translate_quark(), 0,
                                    "malformed ADDRESS_STRING packet");
                return false;
            }
        }

        return true;

    case TRANSLATE_VIEW:
        if (!valid_view_name(payload)) {
            g_set_error_literal(error_r, translate_quark(), 0,
                                "invalid view name");
            return false;
        }

        if (!AddView(payload, error_r))
            return false;

        return true;

    case TRANSLATE_MAX_AGE:
        if (payload_length != 4) {
            g_set_error_literal(error_r, translate_quark(), 0,
                                "malformed MAX_AGE packet");
            return false;
        }

        switch (previous_command) {
        case TRANSLATE_BEGIN:
            response.max_age = std::chrono::seconds(*(const uint32_t *)_payload);
            break;

        case TRANSLATE_USER:
            response.user_max_age = std::chrono::seconds(*(const uint32_t *)_payload);
            break;

        default:
            g_set_error_literal(error_r, translate_quark(), 0,
                                "misplaced MAX_AGE packet");
            return false;
        }

        return true;

    case TRANSLATE_VARY:
        if (payload_length == 0 ||
            payload_length % sizeof(response.vary.data[0]) != 0) {
            g_set_error_literal(error_r, translate_quark(), 0,
                                "malformed VARY packet");
            return false;
        }

        response.vary.data = (const uint16_t *)_payload;
        response.vary.size = payload_length / sizeof(response.vary.data[0]);
        return true;

    case TRANSLATE_INVALIDATE:
        if (payload_length == 0 ||
            payload_length % sizeof(response.invalidate.data[0]) != 0) {
            g_set_error_literal(error_r, translate_quark(), 0,
                                "malformed INVALIDATE packet");
            return false;
        }

        response.invalidate.data = (const uint16_t *)_payload;
        response.invalidate.size = payload_length /
            sizeof(response.invalidate.data[0]);
        return true;

    case TRANSLATE_BASE:
        if (!is_valid_absolute_uri(payload, payload_length) ||
            payload[payload_length - 1] != '/') {
            g_set_error_literal(error_r, translate_quark(), 0,
                                "malformed BASE packet");
            return false;
        }

        if (from_request.uri == nullptr ||
            response.auto_base ||
            response.base != nullptr) {
            g_set_error_literal(error_r, translate_quark(), 0,
                                "misplaced BASE packet");
            return false;
        }

        if (memcmp(from_request.uri, payload, payload_length) != 0) {
            g_set_error_literal(error_r, translate_quark(), 0,
                                "BASE mismatches request URI");
            return false;
        }

        response.base = payload;
        return true;

    case TRANSLATE_UNSAFE_BASE:
        if (payload_length > 0) {
            g_set_error_literal(error_r, translate_quark(), 0,
                                "malformed UNSAFE_BASE packet");
            return false;
        }

        if (response.base == nullptr) {
            g_set_error_literal(error_r, translate_quark(), 0,
                                "misplaced UNSAFE_BASE packet");
            return false;
        }

        response.unsafe_base = true;
        return true;

    case TRANSLATE_EASY_BASE:
        if (payload_length > 0) {
            g_set_error_literal(error_r, translate_quark(), 0,
                                "malformed EASY_BASE");
            return false;
        }

        if (response.base == nullptr) {
            g_set_error_literal(error_r, translate_quark(), 0,
                                "EASY_BASE without BASE");
            return false;
        }

        if (response.easy_base) {
            g_set_error_literal(error_r, translate_quark(), 0,
                                "duplicate EASY_BASE");
            return false;
        }

        response.easy_base = true;
        return true;

    case TRANSLATE_REGEX:
        if (response.base == nullptr) {
            g_set_error_literal(error_r, translate_quark(), 0,
                                "REGEX without BASE");
            return false;
        }

        if (response.regex != nullptr) {
            g_set_error_literal(error_r, translate_quark(), 0,
                                "duplicate REGEX");
            return false;
        }

        if (!is_valid_nonempty_string(payload, payload_length)) {
            g_set_error_literal(error_r, translate_quark(), 0,
                                "malformed REGEX packet");
            return false;
        }

        response.regex = payload;
        return true;

    case TRANSLATE_INVERSE_REGEX:
        if (response.base == nullptr) {
            g_set_error_literal(error_r, translate_quark(), 0,
                                "INVERSE_REGEX without BASE");
            return false;
        }

        if (response.inverse_regex != nullptr) {
            g_set_error_literal(error_r, translate_quark(), 0,
                                "duplicate INVERSE_REGEX");
            return false;
        }

        if (!is_valid_nonempty_string(payload, payload_length)) {
            g_set_error_literal(error_r, translate_quark(), 0,
                                "malformed INVERSE_REGEX packet");
            return false;
        }

        response.inverse_regex = payload;
        return true;

    case TRANSLATE_REGEX_TAIL:
        if (payload_length > 0) {
            g_set_error_literal(error_r, translate_quark(), 0,
                                "malformed REGEX_TAIL packet");
            return false;
        }

        if (response.regex == nullptr &&
            response.inverse_regex == nullptr) {
            g_set_error_literal(error_r, translate_quark(), 0,
                                "misplaced REGEX_TAIL packet");
            return false;
        }

        if (response.regex_tail) {
            g_set_error_literal(error_r, translate_quark(), 0,
                                "duplicate REGEX_TAIL packet");
            return false;
        }

        response.regex_tail = true;
        return true;

    case TRANSLATE_REGEX_UNESCAPE:
        if (payload_length > 0) {
            g_set_error_literal(error_r, translate_quark(), 0,
                                "malformed REGEX_UNESCAPE packet");
            return false;
        }

        if (response.regex == nullptr &&
            response.inverse_regex == nullptr) {
            g_set_error_literal(error_r, translate_quark(), 0,
                                "misplaced REGEX_UNESCAPE packet");
            return false;
        }

        if (response.regex_unescape) {
            g_set_error_literal(error_r, translate_quark(), 0,
                                "duplicate REGEX_UNESCAPE packet");
            return false;
        }

        response.regex_unescape = true;
        return true;

    case TRANSLATE_DELEGATE:
        if (file_address == nullptr) {
            g_set_error_literal(error_r, translate_quark(), 0,
                                "misplaced DELEGATE packet");
            return false;
        }

        if (!is_valid_absolute_path(payload, payload_length)) {
            g_set_error_literal(error_r, translate_quark(), 0,
                                "malformed DELEGATE packet");
            return false;
        }

        file_address->delegate = NewFromPool<DelegateAddress>(*pool, payload);
        child_options = &file_address->delegate->child_options;
        ns_options = &child_options->ns;
        mount_list = &ns_options->mounts;
        jail = &child_options->jail;
        return true;

    case TRANSLATE_APPEND:
        if (!is_valid_nonempty_string(payload, payload_length)) {
            g_set_error_literal(error_r, translate_quark(), 0,
                                "malformed APPEND packet");
            return false;
        }

        if (resource_address == nullptr) {
            g_set_error_literal(error_r, translate_quark(), 0,
                                "misplaced APPEND packet");
            return false;
        }

        if (cgi_address != nullptr) {
            if (cgi_address->args.IsFull()) {
                g_set_error_literal(error_r, translate_quark(), 0,
                                "too many APPEND packets");
                return false;
            }

            cgi_address->args.Append(payload);
            return true;
        } else if (lhttp_address != nullptr) {
            if (lhttp_address->args.IsFull()) {
                g_set_error_literal(error_r, translate_quark(), 0,
                                "too many APPEND packets");
                return false;
            }

            lhttp_address->args.Append(payload);
            return true;
        } else {
            g_set_error_literal(error_r, translate_quark(), 0,
                                "misplaced APPEND packet");
            return false;
        }

    case TRANSLATE_EXPAND_APPEND:
        if (!is_valid_nonempty_string(payload, payload_length)) {
            g_set_error_literal(error_r, translate_quark(), 0,
                                "malformed EXPAND_APPEND packet");
            return false;
        }

        if (response.regex == nullptr ||
            resource_address == nullptr) {
            g_set_error_literal(error_r, translate_quark(), 0,
                                "misplaced EXPAND_APPEND packet");
            return false;
        }

        if (cgi_address != nullptr) {
            if (!cgi_address->args.CanSetExpand()) {
                g_set_error_literal(error_r, translate_quark(), 0,
                                "misplaced EXPAND_APPEND packet");
                return false;
            }

            cgi_address->args.SetExpand(payload);
            return true;
        } else if (lhttp_address != nullptr) {
            if (!lhttp_address->args.CanSetExpand()) {
                g_set_error_literal(error_r, translate_quark(), 0,
                                    "misplaced EXPAND_APPEND packet");
                return false;
            }

            lhttp_address->args.SetExpand(payload);
            return true;
        } else {
            g_set_error_literal(error_r, translate_quark(), 0,
                                "misplaced APPEND packet");
            return false;
        }

    case TRANSLATE_PAIR:
        if (cgi_address != nullptr &&
            resource_address->type != ResourceAddress::Type::CGI &&
            resource_address->type != ResourceAddress::Type::PIPE) {
            return translate_client_pair(cgi_address->params, "PAIR",
                                         payload, payload_length,
                                         error_r);
        } else if (child_options != nullptr) {
            return translate_client_pair(child_options->env, "PAIR",
                                         payload, payload_length,
                                         error_r);

        } else {
            g_set_error_literal(error_r, translate_quark(), 0,
                                "misplaced PAIR packet");
            return false;
        }

    case TRANSLATE_EXPAND_PAIR:
        if (response.regex == nullptr) {
            g_set_error_literal(error_r, translate_quark(), 0,
                                "misplaced EXPAND_PAIR packet");
            return false;
        }

        if (cgi_address != nullptr) {
            const auto type = resource_address->type;
            struct param_array &p = type == ResourceAddress::Type::CGI
                ? cgi_address->options.env
                : cgi_address->params;

            return translate_client_expand_pair(p, "EXPAND_PAIR",
                                                payload, payload_length,
                                                error_r);
        } else if (lhttp_address != nullptr) {
            return translate_client_expand_pair(lhttp_address->options.env,
                                                "EXPAND_PAIR",
                                                payload, payload_length,
                                                error_r);
        } else {
            g_set_error_literal(error_r, translate_quark(), 0,
                                "misplaced EXPAND_PAIR packet");
            return false;
        }

    case TRANSLATE_DISCARD_SESSION:
        response.discard_session = true;
        return true;

    case TRANSLATE_REQUEST_HEADER_FORWARD:
        return view != nullptr
            ? parse_header_forward(&view->request_header_forward,
                                   payload, payload_length, error_r)
            : parse_header_forward(&response.request_header_forward,
                                   payload, payload_length, error_r);

    case TRANSLATE_RESPONSE_HEADER_FORWARD:
        return view != nullptr
            ? parse_header_forward(&view->response_header_forward,
                                   payload, payload_length, error_r)
            : parse_header_forward(&response.response_header_forward,
                                   payload, payload_length, error_r);

    case TRANSLATE_WWW_AUTHENTICATE:
        if (!is_valid_nonempty_string(payload, payload_length)) {
            g_set_error_literal(error_r, translate_quark(), 0,
                                "malformed WWW_AUTHENTICATE packet");
            return false;
        }

        response.www_authenticate = payload;
        return true;

    case TRANSLATE_AUTHENTICATION_INFO:
        if (!is_valid_nonempty_string(payload, payload_length)) {
            g_set_error_literal(error_r, translate_quark(), 0,
                                "malformed AUTHENTICATION_INFO packet");
            return false;
        }

        response.authentication_info = payload;
        return true;

    case TRANSLATE_HEADER:
        if (!parse_header(pool, response.response_headers,
                          "HEADER", payload, payload_length, error_r))
            return false;

        return true;

    case TRANSLATE_SECURE_COOKIE:
        response.secure_cookie = true;
        return true;

    case TRANSLATE_COOKIE_DOMAIN:
        if (response.cookie_domain != nullptr) {
            g_set_error_literal(error_r, translate_quark(), 0,
                                "misplaced COOKIE_DOMAIN packet");
            return false;
        }

        if (!is_valid_nonempty_string(payload, payload_length)) {
            g_set_error_literal(error_r, translate_quark(), 0,
                                "malformed COOKIE_DOMAIN packet");
            return false;
        }

        response.cookie_domain = payload;
        return true;

    case TRANSLATE_ERROR_DOCUMENT:
        response.error_document = { payload, payload_length };
        return true;

    case TRANSLATE_CHECK:
        if (!response.check.IsNull()) {
            g_set_error_literal(error_r, translate_quark(), 0,
                                "duplicate CHECK packet");
            return false;
        }

        response.check = { payload, payload_length };
        return true;

    case TRANSLATE_PREVIOUS:
        response.previous = true;
        return true;

    case TRANSLATE_WAS:
        if (resource_address == nullptr ||
            resource_address->type != ResourceAddress::Type::NONE) {
            g_set_error_literal(error_r, translate_quark(), 0,
                                "misplaced WAS packet");
            return false;
        }

        if (!is_valid_absolute_path(payload, payload_length)) {
            g_set_error_literal(error_r, translate_quark(), 0,
                                "malformed WAS packet");
            return false;
        }

        cgi_address = cgi_address_new(*pool, payload);
        *resource_address = ResourceAddress(ResourceAddress::Type::WAS,
                                           *cgi_address);

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
            g_set_error_literal(error_r, translate_quark(), 0,
                                "misplaced STICKY packet");
            return false;
        }

        address_list->SetStickyMode(StickyMode::SESSION_MODULO);
        return true;

    case TRANSLATE_DUMP_HEADERS:
        response.dump_headers = true;
        return true;

    case TRANSLATE_COOKIE_HOST:
        if (resource_address == nullptr ||
            resource_address->type == ResourceAddress::Type::NONE) {
            g_set_error_literal(error_r, translate_quark(), 0,
                                "misplaced COOKIE_HOST packet");
            return false;
        }

        if (!is_valid_nonempty_string(payload, payload_length)) {
            g_set_error_literal(error_r, translate_quark(), 0,
                                "malformed COOKIE_HOST packet");
            return false;
        }

        response.cookie_host = payload;
        return true;

    case TRANSLATE_COOKIE_PATH:
        if (response.cookie_path != nullptr) {
            g_set_error_literal(error_r, translate_quark(), 0,
                                "misplaced COOKIE_PATH packet");
            return false;
        }

        if (!is_valid_absolute_uri(payload, payload_length)) {
            g_set_error_literal(error_r, translate_quark(), 0,
                                "malformed COOKIE_PATH packet");
            return false;
        }

        response.cookie_path = payload;
        return true;

    case TRANSLATE_PROCESS_CSS:
        new_transformation = AddTransformation();
        new_transformation->type = Transformation::Type::PROCESS_CSS;
        new_transformation->u.css_processor.options = CSS_PROCESSOR_REWRITE_URL;
        return true;

    case TRANSLATE_PREFIX_CSS_CLASS:
        if (transformation == nullptr) {
            g_set_error_literal(error_r, translate_quark(), 0,
                                "misplaced PREFIX_CSS_CLASS packet");
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
            g_set_error_literal(error_r, translate_quark(), 0,
                                "misplaced PREFIX_CSS_CLASS packet");
            return false;
        }

        return true;

    case TRANSLATE_PREFIX_XML_ID:
        if (transformation == nullptr) {
            g_set_error_literal(error_r, translate_quark(), 0,
                                "misplaced PREFIX_XML_ID packet");
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
            g_set_error_literal(error_r, translate_quark(), 0,
                                "misplaced PREFIX_XML_ID packet");
            return false;
        }

        return true;

    case TRANSLATE_PROCESS_STYLE:
        if (transformation == nullptr ||
            transformation->type != Transformation::Type::PROCESS) {
            g_set_error_literal(error_r, translate_quark(), 0,
                                "misplaced PROCESS_STYLE packet");
            return false;
        }

        transformation->u.processor.options |= PROCESSOR_STYLE;
        return true;

    case TRANSLATE_FOCUS_WIDGET:
        if (transformation == nullptr ||
            transformation->type != Transformation::Type::PROCESS) {
            g_set_error_literal(error_r, translate_quark(), 0,
                                "misplaced FOCUS_WIDGET packet");
            return false;
        }

        transformation->u.processor.options |= PROCESSOR_FOCUS_WIDGET;
        return true;

    case TRANSLATE_ANCHOR_ABSOLUTE:
        if (transformation == nullptr ||
            transformation->type != Transformation::Type::PROCESS) {
            g_set_error_literal(error_r, translate_quark(), 0,
                                "misplaced ANCHOR_ABSOLUTE packet");
            return false;
        }

        response.anchor_absolute = true;
        return true;

    case TRANSLATE_PROCESS_TEXT:
        new_transformation = AddTransformation();
        new_transformation->type = Transformation::Type::PROCESS_TEXT;
        return true;

    case TRANSLATE_LOCAL_URI:
        if (response.local_uri != nullptr) {
            g_set_error_literal(error_r, translate_quark(), 0,
                                "misplaced LOCAL_URI packet");
            return false;
        }

        if (payload_length == 0 || payload[payload_length - 1] != '/') {
            g_set_error_literal(error_r, translate_quark(), 0,
                                "malformed LOCAL_URI packet");
            return false;
        }

        response.local_uri = payload;
        return true;

    case TRANSLATE_AUTO_BASE:
        if (resource_address != &response.address ||
            cgi_address == nullptr ||
            cgi_address != &response.address.GetCgi() ||
            cgi_address->path_info == nullptr ||
            from_request.uri == nullptr ||
            response.base != nullptr ||
            response.auto_base) {
            g_set_error_literal(error_r, translate_quark(), 0,
                                "misplaced AUTO_BASE packet");
            return false;
        }

        response.auto_base = true;
        return true;

    case TRANSLATE_VALIDATE_MTIME:
        if (payload_length < 10 || payload[8] != '/' ||
            memchr(payload + 9, 0, payload_length - 9) != nullptr) {
            g_set_error_literal(error_r, translate_quark(), 0,
                                "malformed VALIDATE_MTIME packet");
            return false;
        }

        response.validate_mtime.mtime = *(const uint64_t *)_payload;
        response.validate_mtime.path =
            p_strndup(pool, payload + 8, payload_length - 8);
        return true;

    case TRANSLATE_LHTTP_PATH:
        if (resource_address == nullptr ||
            resource_address->type != ResourceAddress::Type::NONE) {
            g_set_error_literal(error_r, translate_quark(), 0,
                                "misplaced LHTTP_PATH packet");
            return false;
        }

        if (!is_valid_absolute_path(payload, payload_length)) {
            g_set_error_literal(error_r, translate_quark(), 0,
                                "malformed LHTTP_PATH packet");
            return false;
        }

        lhttp_address = NewFromPool<LhttpAddress>(*pool, payload);
        *resource_address = *lhttp_address;
        child_options = &lhttp_address->options;
        ns_options = &child_options->ns;
        mount_list = &ns_options->mounts;
        jail = &child_options->jail;
        return true;

    case TRANSLATE_LHTTP_URI:
        if (lhttp_address == nullptr ||
            lhttp_address->uri != nullptr) {
            g_set_error_literal(error_r, translate_quark(), 0,
                                "misplaced LHTTP_HOST packet");
            return false;
        }

        if (!is_valid_absolute_uri(payload, payload_length)) {
            g_set_error_literal(error_r, translate_quark(), 0,
                                "malformed LHTTP_URI packet");
            return false;
        }

        lhttp_address->uri = payload;
        return true;

    case TRANSLATE_EXPAND_LHTTP_URI:
        if (lhttp_address == nullptr ||
            lhttp_address->uri == nullptr ||
            lhttp_address->expand_uri != nullptr ||
            response.regex == nullptr) {
            g_set_error_literal(error_r, translate_quark(), 0,
                                "misplaced EXPAND_LHTTP_URI packet");
            return false;
        }

        if (!is_valid_nonempty_string(payload, payload_length)) {
            g_set_error_literal(error_r, translate_quark(), 0,
                                "malformed EXPAND_LHTTP_URI packet");
            return false;
        }

        lhttp_address->expand_uri = payload;
        return true;

    case TRANSLATE_LHTTP_HOST:
        if (lhttp_address == nullptr ||
            lhttp_address->host_and_port != nullptr) {
            g_set_error_literal(error_r, translate_quark(), 0,
                                "misplaced LHTTP_HOST packet");
            return false;
        }

        if (!is_valid_nonempty_string(payload, payload_length)) {
            g_set_error_literal(error_r, translate_quark(), 0,
                                "malformed LHTTP_HOST packet");
            return false;
        }

        lhttp_address->host_and_port = payload;
        return true;

    case TRANSLATE_CONCURRENCY:
        if (lhttp_address == nullptr) {
            g_set_error_literal(error_r, translate_quark(), 0,
                                "misplaced CONCURRENCY packet");
            return false;
        }

        if (payload_length != 2) {
            g_set_error_literal(error_r, translate_quark(), 0,
                                "malformed CONCURRENCY packet");
            return false;
        }

        lhttp_address->concurrency = *(const uint16_t *)_payload;
        return true;

    case TRANSLATE_WANT_FULL_URI:
        if (from_request.want_full_uri) {
            g_set_error_literal(error_r, translate_quark(), 0,
                                "WANT_FULL_URI loop");
            return false;
        }

        if (!response.want_full_uri.IsNull()) {
            g_set_error_literal(error_r, translate_quark(), 0,
                                "duplicate WANT_FULL_URI packet");
            return false;
        }

        response.want_full_uri = { payload, payload_length };
        return true;

    case TRANSLATE_USER_NAMESPACE:
        if (payload_length != 0) {
            g_set_error_literal(error_r, translate_quark(), 0,
                                "malformed USER_NAMESPACE packet");
            return false;
        }

        if (ns_options != nullptr) {
            ns_options->enable_user = true;
        } else {
            g_set_error_literal(error_r, translate_quark(), 0,
                                "misplaced USER_NAMESPACE packet");
            return false;
        }

        return true;

    case TRANSLATE_PID_NAMESPACE:
        if (payload_length != 0) {
            g_set_error_literal(error_r, translate_quark(), 0,
                                "malformed PID_NAMESPACE packet");
            return false;
        }

        if (ns_options != nullptr) {
            ns_options->enable_pid = true;
        } else {
            g_set_error_literal(error_r, translate_quark(), 0,
                                "misplaced PID_NAMESPACE packet");
            return false;
        }

        return true;

    case TRANSLATE_NETWORK_NAMESPACE:
        if (payload_length != 0) {
            g_set_error_literal(error_r, translate_quark(), 0,
                                "malformed NETWORK_NAMESPACE packet");
            return false;
        }

        if (ns_options != nullptr) {
            ns_options->enable_network = true;
        } else {
            g_set_error_literal(error_r, translate_quark(), 0,
                                "misplaced NETWORK_NAMESPACE packet");
            return false;
        }

        return true;

    case TRANSLATE_PIVOT_ROOT:
        return translate_client_pivot_root(ns_options, payload, payload_length,
                                           error_r);

    case TRANSLATE_MOUNT_PROC:
        return translate_client_mount_proc(ns_options, payload_length,
                                           error_r);

    case TRANSLATE_MOUNT_HOME:
        return translate_client_mount_home(ns_options, payload, payload_length,
                                           error_r);

    case TRANSLATE_BIND_MOUNT:
        return HandleBindMount(payload, payload_length, false, false, error_r);

    case TRANSLATE_MOUNT_TMP_TMPFS:
        return translate_client_mount_tmp_tmpfs(ns_options,
                                                { payload, payload_length },
                                                error_r);

    case TRANSLATE_UTS_NAMESPACE:
        return translate_client_uts_namespace(ns_options, payload, error_r);

    case TRANSLATE_RLIMITS:
        return translate_client_rlimits(child_options, payload, error_r);

    case TRANSLATE_WANT:
        return HandleWant((const uint16_t *)_payload, payload_length, error_r);

    case TRANSLATE_FILE_NOT_FOUND:
        return translate_client_file_not_found(response,
                                               { _payload, payload_length },
                                               error_r);

    case TRANSLATE_CONTENT_TYPE_LOOKUP:
        return HandleContentTypeLookup({ _payload, payload_length }, error_r);

    case TRANSLATE_DIRECTORY_INDEX:
        return translate_client_directory_index(response,
                                                { _payload, payload_length },
                                                error_r);

    case TRANSLATE_EXPIRES_RELATIVE:
        return translate_client_expires_relative(response,
                                                 { _payload, payload_length },
                                                 error_r);


    case TRANSLATE_TEST_PATH:
        if (!is_valid_absolute_path(payload, payload_length)) {
            g_set_error_literal(error_r, translate_quark(), 0,
                                "malformed TEST_PATH packet");
            return false;
        }

        if (response.test_path != nullptr) {
            g_set_error_literal(error_r, translate_quark(), 0,
                                "duplicate TEST_PATH packet");
            return false;
        }

        response.test_path = payload;
        return true;

    case TRANSLATE_EXPAND_TEST_PATH:
        if (response.regex == nullptr) {
            g_set_error_literal(error_r, translate_quark(), 0,
                                "misplaced EXPAND_TEST_PATH packet");
            return false;
        }

        if (!is_valid_nonempty_string(payload, payload_length)) {
            g_set_error_literal(error_r, translate_quark(), 0,
                                "malformed EXPAND_TEST_PATH packet");
            return false;
        }

        if (response.expand_test_path != nullptr) {
            g_set_error_literal(error_r, translate_quark(), 0,
                                "duplicate EXPAND_TEST_PATH packet");
            return false;
        }

        response.expand_test_path = payload;
        return true;

    case TRANSLATE_REDIRECT_QUERY_STRING:
        if (payload_length != 0) {
            g_set_error_literal(error_r, translate_quark(), 0,
                                "malformed REDIRECT_QUERY_STRING packet");
            return false;
        }

        if (response.redirect_query_string ||
            (response.redirect == nullptr &&
             response.expand_redirect == nullptr)) {
            g_set_error_literal(error_r, translate_quark(), 0,
                                "misplaced REDIRECT_QUERY_STRING packet");
            return false;
        }

        response.redirect_query_string = true;
        return true;

    case TRANSLATE_ENOTDIR:
        return translate_client_enotdir(response, { _payload, payload_length },
                                        error_r);

    case TRANSLATE_STDERR_PATH:
        return translate_client_stderr_path(child_options,
                                            { _payload, payload_length },
                                            error_r);

    case TRANSLATE_AUTH:
        if (response.HasAuth()) {
            g_set_error_literal(error_r, translate_quark(), 0,
                                "duplicate AUTH packet");
            return false;
        }

        response.auth = { payload, payload_length };
        return true;

    case TRANSLATE_SETENV:
        if (child_options != nullptr) {
            return translate_client_pair(child_options->env,
                                         "SETENV",
                                         payload, payload_length,
                                         error_r);
        } else {
            g_set_error_literal(error_r, translate_quark(), 0,
                                "misplaced SETENV packet");
            return false;
        }

    case TRANSLATE_EXPAND_SETENV:
        if (response.regex == nullptr) {
            g_set_error_literal(error_r, translate_quark(), 0,
                                "misplaced EXPAND_SETENV packet");
            return false;
        }

        if (child_options != nullptr) {
            return translate_client_expand_pair(child_options->env,
                                                "EXPAND_SETENV",
                                                payload, payload_length,
                                                error_r);
        } else {
            g_set_error_literal(error_r, translate_quark(), 0,
                                "misplaced SETENV packet");
            return false;
        }

    case TRANSLATE_EXPAND_URI:
        if (response.regex == nullptr ||
            response.uri == nullptr ||
            response.expand_uri != nullptr) {
            g_set_error_literal(error_r, translate_quark(), 0,
                                "misplaced EXPAND_URI packet");
            return false;
        }

        if (!is_valid_nonempty_string(payload, payload_length)) {
            g_set_error_literal(error_r, translate_quark(), 0,
                                "malformed EXPAND_URI packet");
            return false;
        }

        response.expand_uri = payload;
        return true;

    case TRANSLATE_EXPAND_SITE:
        if (response.regex == nullptr ||
            response.site == nullptr ||
            response.expand_site != nullptr) {
            g_set_error_literal(error_r, translate_quark(), 0,
                                "misplaced EXPAND_SITE packet");
            return false;
        }

        if (!is_valid_nonempty_string(payload, payload_length)) {
            g_set_error_literal(error_r, translate_quark(), 0,
                                "malformed EXPAND_SITE packet");
            return false;
        }

        response.expand_site = payload;
        return true;

    case TRANSLATE_REQUEST_HEADER:
        if (!parse_header(pool, response.request_headers,
                          "REQUEST_HEADER", payload, payload_length, error_r))
            return false;

        return true;

    case TRANSLATE_EXPAND_REQUEST_HEADER:
        if (response.regex == nullptr) {
            g_set_error_literal(error_r, translate_quark(), 0,
                                "misplaced EXPAND_REQUEST_HEADERS packet");
            return false;
        }

        if (!parse_header(pool,
                          response.expand_request_headers,
                          "EXPAND_REQUEST_HEADER", payload, payload_length,
                          error_r))
            return false;

        return true;

    case TRANSLATE_AUTO_GZIPPED:
        if (payload_length > 0) {
            g_set_error_literal(error_r, translate_quark(), 0,
                                "malformed AUTO_GZIPPED packet");
            return false;
        }

        if (file_address != nullptr) {
            if (file_address->auto_gzipped ||
                file_address->gzipped != nullptr) {
                g_set_error_literal(error_r, translate_quark(), 0,
                                    "misplaced AUTO_GZIPPED packet");
                return false;
            }

            file_address->auto_gzipped = true;
            return true;
        } else if (nfs_address != nullptr) {
            /* ignore for now */
        } else {
            g_set_error_literal(error_r, translate_quark(), 0,
                                "misplaced AUTO_GZIPPED packet");
            return false;
        }

    case TRANSLATE_PROBE_PATH_SUFFIXES:
        if (!response.probe_path_suffixes.IsNull() ||
            (response.test_path == nullptr &&
             response.expand_test_path == nullptr)) {
            g_set_error_literal(error_r, translate_quark(), 0,
                                "misplaced PROBE_PATH_SUFFIXES packet");
            return false;
        }

        response.probe_path_suffixes = { payload, payload_length };
        return true;

    case TRANSLATE_PROBE_SUFFIX:
        if (response.probe_path_suffixes.IsNull()) {
            g_set_error_literal(error_r, translate_quark(), 0,
                                "misplaced PROBE_SUFFIX packet");
            return false;
        }

        if (response.probe_suffixes.full()) {
            g_set_error_literal(error_r, translate_quark(), 0,
                                "too many PROBE_SUFFIX packets");
            return false;
        }

        if (!CheckProbeSuffix(payload, payload_length)) {
            g_set_error_literal(error_r, translate_quark(), 0,
                                "malformed PROBE_SUFFIX packets");
            return false;
        }

        response.probe_suffixes.push_back(payload);
        return true;

    case TRANSLATE_AUTH_FILE:
        if (response.HasAuth()) {
            g_set_error_literal(error_r, translate_quark(), 0,
                                "duplicate AUTH_FILE packet");
            return false;
        }

        if (!is_valid_absolute_path(payload, payload_length)) {
            g_set_error_literal(error_r, translate_quark(), 0,
                                "malformed AUTH_FILE packet");
            return false;
        }

        response.auth_file = payload;
        return true;

    case TRANSLATE_EXPAND_AUTH_FILE:
        if (response.HasAuth()) {
            g_set_error_literal(error_r, translate_quark(), 0,
                                "duplicate EXPAND_AUTH_FILE packet");
            return false;
        }

        if (!is_valid_nonempty_string(payload, payload_length)) {
            g_set_error_literal(error_r, translate_quark(), 0,
                                "malformed EXPAND_AUTH_FILE packet");
            return false;
        }

        if (response.regex == nullptr) {
            g_set_error_literal(error_r, translate_quark(), 0,
                                "misplaced EXPAND_AUTH_FILE packet");
            return false;
        }

        response.expand_auth_file = payload;
        return true;

    case TRANSLATE_APPEND_AUTH:
        if (!response.HasAuth() ||
            !response.append_auth.IsNull() ||
            response.expand_append_auth != nullptr) {
            g_set_error_literal(error_r, translate_quark(), 0,
                                "misplaced APPEND_AUTH packet");
            return false;
        }

        response.append_auth = { payload, payload_length };
        return true;

    case TRANSLATE_EXPAND_APPEND_AUTH:
        if (response.regex == nullptr ||
            !response.HasAuth() ||
            !response.append_auth.IsNull() ||
            response.expand_append_auth != nullptr) {
            g_set_error_literal(error_r, translate_quark(), 0,
                                "misplaced EXPAND_APPEND_AUTH packet");
            return false;
        }

        if (!is_valid_nonempty_string(payload, payload_length)) {
            g_set_error_literal(error_r, translate_quark(), 0,
                                "malformed EXPAND_APPEND_AUTH packet");
            return false;
        }

        response.expand_append_auth = payload;
        return true;

    case TRANSLATE_EXPAND_COOKIE_HOST:
        if (response.regex == nullptr ||
            resource_address == nullptr ||
            resource_address->type == ResourceAddress::Type::NONE) {
            g_set_error_literal(error_r, translate_quark(), 0,
                                "misplaced EXPAND_COOKIE_HOST packet");
            return false;
        }

        if (!is_valid_nonempty_string(payload, payload_length)) {
            g_set_error_literal(error_r, translate_quark(), 0,
                                "malformed EXPAND_COOKIE_HOST packet");
            return false;
        }

        response.expand_cookie_host = payload;
        return true;

    case TRANSLATE_EXPAND_BIND_MOUNT:
        return HandleBindMount(payload, payload_length, true, false, error_r);

    case TRANSLATE_NON_BLOCKING:
        if (payload_length > 0) {
            g_set_error_literal(error_r, translate_quark(), 0,
                                "malformed NON_BLOCKING packet");
            return false;
        }

        if (lhttp_address != nullptr) {
            lhttp_address->blocking = false;
            return true;
        } else {
            g_set_error_literal(error_r, translate_quark(), 0,
                                "misplaced NON_BLOCKING packet");
            return false;
        }

    case TRANSLATE_READ_FILE:
        if (response.read_file != nullptr ||
            response.expand_read_file != nullptr) {
            g_set_error_literal(error_r, translate_quark(), 0,
                                "duplicate READ_FILE packet");
            return false;
        }

        if (!is_valid_absolute_path(payload, payload_length)) {
            g_set_error_literal(error_r, translate_quark(), 0,
                                "malformed READ_FILE packet");
            return false;
        }

        response.read_file = payload;
        return true;

    case TRANSLATE_EXPAND_READ_FILE:
        if (response.read_file != nullptr ||
            response.expand_read_file != nullptr) {
            g_set_error_literal(error_r, translate_quark(), 0,
                                "duplicate EXPAND_READ_FILE packet");
            return false;
        }

        if (!is_valid_nonempty_string(payload, payload_length)) {
            g_set_error_literal(error_r, translate_quark(), 0,
                                "malformed EXPAND_READ_FILE packet");
            return false;
        }

        response.expand_read_file = payload;
        return true;

    case TRANSLATE_EXPAND_HEADER:
        if (response.regex == nullptr) {
            g_set_error_literal(error_r, translate_quark(), 0,
                                "misplaced EXPAND_HEADER packet");
            return false;
        }

        if (!parse_header(pool,
                          response.expand_response_headers,
                          "EXPAND_HEADER", payload, payload_length,
                          error_r))
            return false;

        return true;

    case TRANSLATE_REGEX_ON_HOST_URI:
        if (response.regex == nullptr &&
            response.inverse_regex == nullptr) {
            g_set_error_literal(error_r, translate_quark(), 0,
                                "REGEX_ON_HOST_URI without REGEX");
            return false;
        }

        if (response.regex_on_host_uri) {
            g_set_error_literal(error_r, translate_quark(), 0,
                                "duplicate REGEX_ON_HOST_URI");
            return false;
        }

        if (payload_length > 0) {
            g_set_error_literal(error_r, translate_quark(), 0,
                                "malformed REGEX_ON_HOST_URI packet");
            return false;
        }

        response.regex_on_host_uri = true;
        return true;

    case TRANSLATE_SESSION_SITE:
        response.session_site = payload;
        return true;

    case TRANSLATE_IPC_NAMESPACE:
        if (payload_length != 0) {
            g_set_error_literal(error_r, translate_quark(), 0,
                                "malformed IPC_NAMESPACE packet");
            return false;
        }

        if (ns_options != nullptr) {
            ns_options->enable_ipc = true;
        } else {
            g_set_error_literal(error_r, translate_quark(), 0,
                                "misplaced IPC_NAMESPACE packet");
            return false;
        }

        return true;

    case TRANSLATE_AUTO_DEFLATE:
        if (payload_length > 0) {
            g_set_error_literal(error_r, translate_quark(), 0,
                                "malformed AUTO_DEFLATE packet");
            return false;
        }

        if (response.auto_deflate) {
            g_set_error_literal(error_r, translate_quark(), 0,
                                "misplaced AUTO_DEFLATE packet");
            return false;
        }

        response.auto_deflate = true;
        return true;

    case TRANSLATE_EXPAND_HOME:
        return translate_client_expand_home(ns_options, jail,
                                            payload, payload_length,
                                            error_r);


    case TRANSLATE_EXPAND_STDERR_PATH:
        return translate_client_expand_stderr_path(child_options,
                                                   { _payload, payload_length },
                                                   error_r);

    case TRANSLATE_REGEX_ON_USER_URI:
        if (response.regex == nullptr &&
            response.inverse_regex == nullptr) {
            g_set_error_literal(error_r, translate_quark(), 0,
                                "REGEX_ON_USER_URI without REGEX");
            return false;
        }

        if (response.regex_on_user_uri) {
            g_set_error_literal(error_r, translate_quark(), 0,
                                "duplicate REGEX_ON_USER_URI");
            return false;
        }

        if (payload_length > 0) {
            g_set_error_literal(error_r, translate_quark(), 0,
                                "malformed REGEX_ON_USER_URI packet");
            return false;
        }

        response.regex_on_user_uri = true;
        return true;

    case TRANSLATE_AUTO_GZIP:
        if (payload_length > 0) {
            g_set_error_literal(error_r, translate_quark(), 0,
                                "malformed AUTO_GZIP packet");
            return false;
        }

        if (response.auto_gzip) {
            g_set_error_literal(error_r, translate_quark(), 0,
                                "misplaced AUTO_GZIP packet");
            return false;
        }

        response.auto_gzip = true;
        return true;

    case TRANSLATE_INTERNAL_REDIRECT:
        if (!response.internal_redirect.IsNull()) {
            g_set_error_literal(error_r, translate_quark(), 0,
                                "duplicate INTERNAL_REDIRECT packet");
            return false;
        }

        response.internal_redirect = { payload, payload_length };
        return true;

    case TRANSLATE_REFENCE:
        return HandleRefence({payload, payload_length}, error_r);

    case TRANSLATE_INVERSE_REGEX_UNESCAPE:
        if (payload_length > 0) {
            g_set_error_literal(error_r, translate_quark(), 0,
                                "malformed INVERSE_REGEX_UNESCAPE packet");
            return false;
        }

        if (response.inverse_regex == nullptr) {
            g_set_error_literal(error_r, translate_quark(), 0,
                                "misplaced INVERSE_REGEX_UNESCAPE packet");
            return false;
        }

        if (response.inverse_regex_unescape) {
            g_set_error_literal(error_r, translate_quark(), 0,
                                "duplicate INVERSE_REGEX_UNESCAPE packet");
            return false;
        }

        response.inverse_regex_unescape = true;
        return true;

    case TRANSLATE_BIND_MOUNT_RW:
        return HandleBindMount(payload, payload_length, false, true, error_r);

    case TRANSLATE_EXPAND_BIND_MOUNT_RW:
        return HandleBindMount(payload, payload_length, true, true, error_r);

    case TRANSLATE_UNTRUSTED_RAW_SITE_SUFFIX:
        if (!is_valid_nonempty_string(payload, payload_length) ||
            payload[payload_length - 1] == '.') {
            g_set_error_literal(error_r, translate_quark(), 0,
                                "malformed UNTRUSTED_RAW_SITE_SUFFIX packet");
            return false;
        }

        if (response.HasUntrusted()) {
            g_set_error_literal(error_r, translate_quark(), 0,
                                "misplaced UNTRUSTED_RAW_SITE_SUFFIX packet");
            return false;
        }

        response.untrusted_raw_site_suffix = payload;
        return true;

    case TRANSLATE_MOUNT_TMPFS:
        return translate_client_mount_tmpfs(ns_options,
                                            payload, payload_length,
                                            error_r);

    case TRANSLATE_REVEAL_USER:
        if (payload_length > 0) {
            g_set_error_literal(error_r, translate_quark(), 0,
                                "malformed REVEAL_USER packet");
            return false;
        }

        if (transformation == nullptr ||
            transformation->type != Transformation::Type::FILTER ||
            transformation->u.filter.reveal_user) {
            g_set_error_literal(error_r, translate_quark(), 0,
                                "misplaced REVEAL_USER packet");
            return false;
        }

        transformation->u.filter.reveal_user = true;
        return true;

    case TRANSLATE_REALM_FROM_AUTH_BASE:
        if (payload_length > 0) {
            g_set_error_literal(error_r, translate_quark(), 0,
                                "malformed REALM_FROM_AUTH_BASE packet");
            return false;
        }

        if (response.realm_from_auth_base) {
            g_set_error_literal(error_r, translate_quark(), 0,
                                "duplicate REALM_FROM_AUTH_BASE packet");
            return false;
        }

        if (response.realm != nullptr || !response.HasAuth()) {
            g_set_error_literal(error_r, translate_quark(), 0,
                                "misplaced REALM_FROM_AUTH_BASE packet");
            return false;
        }

        response.realm_from_auth_base = true;
        return true;

    case TRANSLATE_NO_NEW_PRIVS:
        if (child_options == nullptr || child_options->no_new_privs) {
            g_set_error_literal(error_r, translate_quark(), 0,
                                "misplaced NO_NEW_PRIVS packet");
            return false;
        }

        if (payload_length != 0) {
            g_set_error_literal(error_r, translate_quark(), 0,
                                "malformed NO_NEW_PRIVS packet");
            return false;
        }

        child_options->no_new_privs = true;
        return true;

    case TRANSLATE_CGROUP:
        if (child_options == nullptr ||
            child_options->cgroup.name != nullptr) {
            g_set_error_literal(error_r, translate_quark(), 0,
                                "misplaced CGROUP packet");
            return false;
        }

        if (!valid_view_name(payload)) {
            g_set_error_literal(error_r, translate_quark(), 0,
                                "malformed CGROUP packet");
            return false;
        }

        child_options->cgroup.name = payload;
        return true;

    case TRANSLATE_CGROUP_SET:
        return HandleCgroupSet({payload, payload_length}, error_r);
    }

    g_set_error(error_r, translate_quark(), 0,
                "unknown translation packet: %u", command);
    return false;
}

inline TranslateParser::Result
TranslateParser::HandlePacket(enum beng_translation_command command,
                              const void *const payload, size_t payload_length,
                              GError **error_r)
{
    assert(payload != nullptr);

    if (command == TRANSLATE_BEGIN) {
        if (response.status != (http_status_t)-1) {
            g_set_error_literal(error_r, translate_quark(), 0,
                                "double BEGIN from translation server");
            return Result::ERROR;
        }
    } else {
        if (response.status == (http_status_t)-1) {
            g_set_error_literal(error_r, translate_quark(), 0,
                                "no BEGIN from translation server");
            return Result::ERROR;
        }
    }

    switch (command) {
    case TRANSLATE_END:
        if (!translate_response_finish(&response, error_r))
            return Result::ERROR;

        if (!FinishView(error_r))
            return Result::ERROR;

        return Result::DONE;

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
            response.protocol_version = *(const uint8_t *)payload;

        return Result::MORE;

    default:
        return HandleRegularPacket(command, payload, payload_length, error_r)
            ? Result::MORE
            : Result::ERROR;
    }
}

TranslateParser::Result
TranslateParser::Process(GError **error_r)
{
    if (!reader.IsComplete())
        /* need more data */
        return Result::MORE;

    return HandlePacket(reader.GetCommand(),
                        reader.GetPayload(), reader.GetLength(),
                        error_r);
}
