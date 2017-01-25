/*
 * Parse translation response packets.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "Parser.hxx"
#if TRANSLATION_ENABLE_TRANSFORMATION
#include "Transformation.hxx"
#include "processor.hxx"
#include "css_processor.hxx"
#endif
#if TRANSLATION_ENABLE_WIDGET
#include "widget_class.hxx"
#endif
#if TRANSLATION_ENABLE_RADDRESS
#include "file_address.hxx"
#include "delegate/Address.hxx"
#include "lhttp_address.hxx"
#include "http_address.hxx"
#include "cgi_address.hxx"
#include "nfs_address.hxx"
#endif
#include "spawn/ChildOptions.hxx"
#include "spawn/mount_list.hxx"
#include "spawn/ResourceLimits.hxx"
#if TRANSLATION_ENABLE_JAILCGI
#include "spawn/JailParams.hxx"
#endif
#include "beng-proxy/translation.h"
#if TRANSLATION_ENABLE_HTTP
#include "net/SocketAddress.hxx"
#include "net/AddressInfo.hxx"
#include "net/Resolver.hxx"
#endif
#include "util/CharUtil.hxx"
#include "util/RuntimeError.hxx"

#if TRANSLATION_ENABLE_HTTP
#include <http/header.h>
#endif

#include <assert.h>
#include <string.h>
#include <sys/un.h>
#include <netdb.h>

void
TranslateParser::SetChildOptions(ChildOptions &_child_options)
{
    child_options = &_child_options;
    ns_options = &child_options->ns;
    mount_list = &ns_options->mounts;
    jail = nullptr;
    env_builder = child_options->env;
}

#if TRANSLATION_ENABLE_RADDRESS

void
TranslateParser::SetCgiAddress(ResourceAddress::Type type,
                               const char *path)
{
    cgi_address = alloc.New<CgiAddress>(path);

    *resource_address = ResourceAddress(type, *cgi_address);

    args_builder = cgi_address->args;
    params_builder = cgi_address->params;
    SetChildOptions(cgi_address->options);
}

#endif

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

#if TRANSLATION_ENABLE_HTTP

gcc_pure
static bool
is_valid_absolute_uri(const char *p, size_t size)
{
    return is_valid_absolute_path(p, size);
}

#endif

#if TRANSLATION_ENABLE_TRANSFORMATION

Transformation *
TranslateParser::AddTransformation()
{
    auto t = alloc.New<Transformation>();
    t->next = nullptr;

    transformation = t;
    *transformation_tail = t;
    transformation_tail = &t->next;

    return t;
}

ResourceAddress *
TranslateParser::AddFilter()
{
    auto *t = AddTransformation();
    t->type = Transformation::Type::FILTER;
    t->u.filter.address = nullptr;
    t->u.filter.reveal_user = false;
    return &t->u.filter.address;
}

#endif

#if TRANSLATION_ENABLE_HTTP

static void
parse_address_string(AllocatorPtr alloc, AddressList *list,
                     const char *p, int default_port)
{
    if (*p == '/' || *p == '@') {
        /* unix domain socket */

        struct sockaddr_un sun;
        size_t path_length = strlen(p);

        if (path_length >= sizeof(sun.sun_path))
            throw std::runtime_error("Socket path is too long");

        sun.sun_family = AF_UNIX;
        memcpy(sun.sun_path, p, path_length + 1);

        socklen_t size = SUN_LEN(&sun);

        if (*p == '@')
            /* abstract socket */
            sun.sun_path[0] = 0;

        list->Add(alloc, { (const struct sockaddr *)&sun, size });
        return;
    }

    struct addrinfo hints;
    memset(&hints, 0, sizeof(hints));
    hints.ai_flags = AI_NUMERICHOST;
    hints.ai_socktype = SOCK_STREAM;

    for (const auto &i : Resolve(p, default_port, &hints))
        list->Add(alloc, i);
}

#endif

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

#if TRANSLATION_ENABLE_WIDGET

void
TranslateParser::FinishView()
{
    assert(response.views != nullptr);

    WidgetView *v = view;
    if (view == nullptr) {
        v = response.views;
        assert(v != nullptr);

        const ResourceAddress *address = &response.address;
        if (address->IsDefined() && !v->address.IsDefined()) {
            /* no address yet: copy address from response */
            v->address.CopyFrom(alloc, *address);
            v->filter_4xx = response.filter_4xx;
        }

        v->request_header_forward = response.request_header_forward;
        v->response_header_forward = response.response_header_forward;
    } else {
        if (!v->address.IsDefined() && v != response.views)
            /* no address yet: inherits settings from the default view */
            v->InheritFrom(alloc, *response.views);
    }

    v->address.Check();
}

inline void
TranslateParser::AddView(const char *name)
{
    FinishView();

    auto new_view = alloc.New<WidgetView>();
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
}

#endif

#if TRANSLATION_ENABLE_HTTP

static void
parse_header_forward(struct header_forward_settings *settings,
                     const void *payload, size_t payload_length)
{
    const beng_header_forward_packet *packet =
        (const beng_header_forward_packet *)payload;

    if (payload_length % sizeof(*packet) != 0)
        throw std::runtime_error("malformed header forward packet");

    while (payload_length > 0) {
        if (packet->group < HEADER_GROUP_ALL ||
            packet->group >= HEADER_GROUP_MAX ||
            (packet->mode != HEADER_FORWARD_NO &&
             packet->mode != HEADER_FORWARD_YES &&
             packet->mode != HEADER_FORWARD_BOTH &&
             packet->mode != HEADER_FORWARD_MANGLE) ||
            packet->reserved != 0)
            throw std::runtime_error("malformed header forward packet");

        if (packet->group == HEADER_GROUP_ALL) {
            for (unsigned i = 0; i < HEADER_GROUP_MAX; ++i)
                if (i != HEADER_GROUP_SECURE)
                    settings->modes[i] = beng_header_forward_mode(packet->mode);
        } else
            settings->modes[packet->group] = beng_header_forward_mode(packet->mode);

        ++packet;
        payload_length -= sizeof(*packet);
    }
}

static void
parse_header(AllocatorPtr alloc,
             KeyValueList &headers, const char *packet_name,
             const char *payload, size_t payload_length)
{
    const char *value = (const char *)memchr(payload, ':', payload_length);
    if (value == nullptr || value == payload ||
        has_null_byte(payload, payload_length))
        throw FormatRuntimeError("malformed %s packet", packet_name);

    const char *name = alloc.DupToLower(StringView(payload, value));
    ++value;

    if (!http_header_name_valid(name))
        throw FormatRuntimeError("malformed name in %s packet", packet_name);
    else if (http_header_is_hop_by_hop(name))
        throw FormatRuntimeError("hop-by-hop %s packet", packet_name);

    headers.Add(alloc, name, value);
}

#endif

#if TRANSLATION_ENABLE_JAILCGI

/**
 * Throws std::runtime_error on error.
 */
static void
translate_jail_finish(JailParams *jail,
                      const TranslateResponse *response,
                      const char *document_root)
{
    if (jail == nullptr || !jail->enabled)
        return;

    if (jail->home_directory == nullptr)
        jail->home_directory = document_root;

    if (jail->home_directory == nullptr)
        throw std::runtime_error("No home directory for JAIL");

    if (jail->site_id == nullptr)
        jail->site_id = response->site;
}

#endif

/**
 * Final fixups for the response before it is passed to the handler.
 *
 * Throws std::runtime_error on error.
 */
static void
translate_response_finish(TranslateResponse *response)
{
#if TRANSLATION_ENABLE_RADDRESS
    if (response->easy_base && !response->address.IsValidBase())
        /* EASY_BASE was enabled, but the resource address does not
           end with a slash, thus LoadBase() cannot work */
        throw std::runtime_error("Invalid base address");

    if (response->address.IsCgiAlike()) {
        auto &cgi = response->address.GetCgi();

        if (cgi.uri == nullptr)
            cgi.uri = response->uri;

        if (cgi.expand_uri == nullptr)
            cgi.expand_uri = response->expand_uri;

        if (cgi.document_root == nullptr)
            cgi.document_root = response->document_root;

        translate_jail_finish(cgi.options.jail,
                              response, cgi.document_root);
    } else if (response->address.type == ResourceAddress::Type::LOCAL) {
        auto &file = response->address.GetFile();

        if (file.delegate != nullptr) {
            if (file.delegate->child_options.jail != nullptr &&
                file.delegate->child_options.jail->enabled &&
                file.document_root == nullptr)
                file.document_root = response->document_root;

            translate_jail_finish(file.delegate->child_options.jail,
                                  response,
                                  file.document_root);
        }
    }

    response->address.Check();
#endif

#if TRANSLATION_ENABLE_HTTP
    /* these lists are in reverse order because new items were added
       to the front; reverse them now */
    response->request_headers.Reverse();
    response->response_headers.Reverse();
#endif

    if (!response->probe_path_suffixes.IsNull() &&
        response->probe_suffixes.empty())
        throw std::runtime_error("PROBE_PATH_SUFFIX without PROBE_SUFFIX");

#if TRANSLATION_ENABLE_HTTP
    if (!response->internal_redirect.IsNull() &&
        (response->uri == nullptr && response->expand_uri == nullptr))
        throw std::runtime_error("INTERNAL_REDIRECT without URI");

    if (!response->internal_redirect.IsNull() &&
        !response->want_full_uri.IsNull())
        throw std::runtime_error("INTERNAL_REDIRECT conflicts with WANT_FULL_URI");
#endif
}

gcc_pure
static bool
translate_client_check_pair(const char *payload, size_t payload_length)
{
    return payload_length > 0 && *payload != '=' &&
        !has_null_byte(payload, payload_length) &&
        strchr(payload + 1, '=') != nullptr;
}

static void
translate_client_check_pair(const char *name,
                            const char *payload, size_t payload_length)
{
    if (!translate_client_check_pair(payload, payload_length))
        throw FormatRuntimeError("malformed %s packet", name);
}

static void
translate_client_pair(AllocatorPtr alloc,
                      ExpandableStringList::Builder &builder,
                      const char *name,
                      const char *payload, size_t payload_length)
{
    translate_client_check_pair(name, payload, payload_length);

    builder.Add(alloc, payload, false);
}

#if TRANSLATION_ENABLE_EXPAND

static void
translate_client_expand_pair(ExpandableStringList::Builder &builder,
                             const char *name,
                             const char *payload, size_t payload_length)
{
    if (!builder.CanSetExpand())
        throw FormatRuntimeError("misplaced %s packet", name);

    translate_client_check_pair(name, payload, payload_length);

    builder.SetExpand(payload);
}

#endif

static void
translate_client_pivot_root(NamespaceOptions *ns,
                            const char *payload, size_t payload_length)
{
    if (!is_valid_absolute_path(payload, payload_length))
        throw std::runtime_error("malformed PIVOT_ROOT packet");

    if (ns == nullptr || ns->pivot_root != nullptr)
        throw std::runtime_error("misplaced PIVOT_ROOT packet");

    ns->enable_mount = true;
    ns->pivot_root = payload;
}

static void
translate_client_home(NamespaceOptions *ns,
#if TRANSLATION_ENABLE_JAILCGI
                      JailParams *jail,
#endif
                      const char *payload, size_t payload_length)
{
    if (!is_valid_absolute_path(payload, payload_length))
        throw std::runtime_error("malformed HOME packet");

    bool ok = false;

    if (ns != nullptr && ns->home == nullptr) {
        ns->home = payload;
        ok = true;
    }

#if TRANSLATION_ENABLE_JAILCGI
    if (jail != nullptr && jail->enabled && jail->home_directory == nullptr) {
        jail->home_directory = payload;
        ok = true;
    }
#endif

    if (!ok)
        throw std::runtime_error("misplaced HOME packet");
}

#if TRANSLATION_ENABLE_EXPAND

static void
translate_client_expand_home(NamespaceOptions *ns,
#if TRANSLATION_ENABLE_JAILCGI
                             JailParams *jail,
#endif
                             const char *payload, size_t payload_length)
{
    if (!is_valid_absolute_path(payload, payload_length))
        throw std::runtime_error("malformed EXPAND_HOME packet");

    bool ok = false;

    if (ns != nullptr && ns->expand_home == nullptr) {
        ns->expand_home = payload;
        ok = true;
    }

#if TRANSLATION_ENABLE_JAILCGI
    if (jail != nullptr && jail->enabled &&
        !jail->expand_home_directory) {
        jail->home_directory = payload;
        jail->expand_home_directory = true;
        ok = true;
    }
#endif

    if (!ok)
        throw std::runtime_error("misplaced EXPAND_HOME packet");
}

#endif

static void
translate_client_mount_proc(NamespaceOptions *ns,
                            size_t payload_length)
{
    if (payload_length > 0)
        throw std::runtime_error("malformed MOUNT_PROC packet");

    if (ns == nullptr || ns->mount_proc)
        throw std::runtime_error("misplaced MOUNT_PROC packet");

    ns->enable_mount = true;
    ns->mount_proc = true;
}

static void
translate_client_mount_tmp_tmpfs(NamespaceOptions *ns,
                                 ConstBuffer<char> payload)
{
    if (has_null_byte(payload.data, payload.size))
        throw std::runtime_error("malformed MOUNT_TMP_TMPFS packet");

    if (ns == nullptr || ns->mount_tmp_tmpfs != nullptr)
        throw std::runtime_error("misplaced MOUNT_TMP_TMPFS packet");

    ns->enable_mount = true;
    ns->mount_tmp_tmpfs = payload.data != nullptr
        ? payload.data
        : "";
}

static void
translate_client_mount_home(NamespaceOptions *ns,
                            const char *payload, size_t payload_length)
{
    if (!is_valid_absolute_path(payload, payload_length))
        throw std::runtime_error("malformed MOUNT_HOME packet");

    if (ns == nullptr || ns->home == nullptr ||
        ns->mount_home != nullptr)
        throw std::runtime_error("misplaced MOUNT_HOME packet");

    ns->enable_mount = true;
    ns->mount_home = payload;
}

static void
translate_client_mount_tmpfs(NamespaceOptions *ns,
                             const char *payload, size_t payload_length)
{
    if (!is_valid_absolute_path(payload, payload_length) ||
        /* not allowed for /tmp, use TRANSLATE_MOUNT_TMP_TMPFS
           instead! */
        strcmp(payload, "/tmp") == 0)
        throw std::runtime_error("malformed MOUNT_TMPFS packet");

    if (ns == nullptr || ns->mount_tmpfs != nullptr)
        throw std::runtime_error("misplaced MOUNT_TMPFS packet");

    ns->enable_mount = true;
    ns->mount_tmpfs = payload;
}

inline void
TranslateParser::HandleBindMount(const char *payload, size_t payload_length,
                                 bool expand, bool writable, bool exec)
{
    if (*payload != '/')
        throw std::runtime_error("malformed BIND_MOUNT packet");

    const char *separator = (const char *)memchr(payload, 0, payload_length);
    if (separator == nullptr || separator[1] != '/')
        throw std::runtime_error("malformed BIND_MOUNT packet");

    if (mount_list == nullptr)
        throw std::runtime_error("misplaced BIND_MOUNT packet");

    auto *m = alloc.New<MountList>(/* skip the slash to make it relative */
                                   payload + 1,
                                   separator + 1,
                                   expand, writable, exec);
    *mount_list = m;
    mount_list = &m->next;
}

static void
translate_client_uts_namespace(NamespaceOptions *ns,
                               const char *payload)
{
    if (*payload == 0)
        throw std::runtime_error("malformed MOUNT_UTS_NAMESPACE packet");

    if (ns == nullptr || ns->hostname != nullptr)
        throw std::runtime_error("misplaced MOUNT_UTS_NAMESPACE packet");

    ns->hostname = payload;
}

static void
translate_client_rlimits(AllocatorPtr alloc, ChildOptions *child_options,
                         const char *payload)
{
    if (child_options == nullptr)
        throw std::runtime_error("misplaced RLIMITS packet");

    if (child_options->rlimits == nullptr)
        child_options->rlimits = alloc.New<ResourceLimits>();

    if (!child_options->rlimits->Parse(payload))
        throw std::runtime_error("malformed RLIMITS packet");
}

inline void
TranslateParser::HandleWant(const uint16_t *payload, size_t payload_length)
{
    if (response.protocol_version < 1)
        throw std::runtime_error("WANT requires protocol version 1");

    if (from_request.want)
        throw std::runtime_error("WANT loop");

    if (!response.want.IsEmpty())
        throw std::runtime_error("duplicate WANT packet");

    if (payload_length % sizeof(payload[0]) != 0)
        throw std::runtime_error("malformed WANT packet");

    response.want = { payload, payload_length / sizeof(payload[0]) };
}

#if TRANSLATION_ENABLE_RADDRESS

static void
translate_client_file_not_found(TranslateResponse &response,
                                ConstBuffer<void> payload)
{
    if (!response.file_not_found.IsNull())
        throw std::runtime_error("duplicate FILE_NOT_FOUND packet");

    if (response.test_path == nullptr &&
        response.expand_test_path == nullptr) {
        switch (response.address.type) {
        case ResourceAddress::Type::NONE:
            throw std::runtime_error("FIlE_NOT_FOUND without resource address");

        case ResourceAddress::Type::HTTP:
        case ResourceAddress::Type::PIPE:
            throw std::runtime_error("FIlE_NOT_FOUND not compatible with resource address");

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
}

inline void
TranslateParser::HandleContentTypeLookup(ConstBuffer<void> payload)
{
    const char *content_type;
    ConstBuffer<void> *content_type_lookup;

    if (file_address != nullptr) {
        content_type = file_address->content_type;
        content_type_lookup = &file_address->content_type_lookup;
    } else if (nfs_address != nullptr) {
        content_type = nfs_address->content_type;
        content_type_lookup = &nfs_address->content_type_lookup;
    } else
        throw std::runtime_error("misplaced CONTENT_TYPE_LOOKUP");

    if (!content_type_lookup->IsNull())
        throw std::runtime_error("duplicate CONTENT_TYPE_LOOKUP");

    if (content_type != nullptr)
        throw std::runtime_error("CONTENT_TYPE/CONTENT_TYPE_LOOKUP conflict");

    *content_type_lookup = payload;
}

static void
translate_client_enotdir(TranslateResponse &response,
                         ConstBuffer<void> payload)
{
    if (!response.enotdir.IsNull())
        throw std::runtime_error("duplicate ENOTDIR");

    if (response.test_path == nullptr) {
        switch (response.address.type) {
        case ResourceAddress::Type::NONE:
            throw std::runtime_error("ENOTDIR without resource address");

        case ResourceAddress::Type::HTTP:
        case ResourceAddress::Type::PIPE:
        case ResourceAddress::Type::NFS:
            throw std::runtime_error("ENOTDIR not compatible with resource address");

        case ResourceAddress::Type::LOCAL:
        case ResourceAddress::Type::CGI:
        case ResourceAddress::Type::FASTCGI:
        case ResourceAddress::Type::WAS:
        case ResourceAddress::Type::LHTTP:
            break;
        }
    }

    response.enotdir = payload;
}

static void
translate_client_directory_index(TranslateResponse &response,
                                 ConstBuffer<void> payload)
{
    if (!response.directory_index.IsNull())
        throw std::runtime_error("duplicate DIRECTORY_INDEX");

    if (response.test_path == nullptr &&
        response.expand_test_path == nullptr) {
        switch (response.address.type) {
        case ResourceAddress::Type::NONE:
            throw std::runtime_error("DIRECTORY_INDEX without resource address");

        case ResourceAddress::Type::HTTP:
        case ResourceAddress::Type::LHTTP:
        case ResourceAddress::Type::PIPE:
        case ResourceAddress::Type::CGI:
        case ResourceAddress::Type::FASTCGI:
        case ResourceAddress::Type::WAS:
            throw std::runtime_error("DIRECTORY_INDEX not compatible with resource address");

        case ResourceAddress::Type::LOCAL:
        case ResourceAddress::Type::NFS:
            break;
        }
    }

    response.directory_index = payload;
}

#endif

static void
translate_client_expires_relative(TranslateResponse &response,
                                  ConstBuffer<void> payload)
{
    if (response.expires_relative > std::chrono::seconds::zero())
        throw std::runtime_error("duplicate EXPIRES_RELATIVE");

    if (payload.size != sizeof(uint32_t))
        throw std::runtime_error("malformed EXPIRES_RELATIVE");

    response.expires_relative = std::chrono::seconds(*(const uint32_t *)payload.data);
}

static void
translate_client_stderr_path(ChildOptions *child_options,
                             ConstBuffer<void> payload)
{
    const char *path = (const char *)payload.data;
    if (!is_valid_absolute_path(path, payload.size))
        throw std::runtime_error("malformed STDERR_PATH packet");

    if (child_options == nullptr || child_options->stderr_null)
        throw std::runtime_error("misplaced STDERR_PATH packet");

    if (child_options->stderr_path != nullptr)
        throw std::runtime_error("duplicate STDERR_PATH packet");

    child_options->stderr_path = path;
}

#if TRANSLATION_ENABLE_EXPAND

static void
translate_client_expand_stderr_path(ChildOptions *child_options,
                                    ConstBuffer<void> payload)
{
    const char *path = (const char *)payload.data;
    if (!is_valid_nonempty_string((const char *)payload.data, payload.size))
        throw std::runtime_error("malformed EXPAND_STDERR_PATH packet");

    if (child_options == nullptr)
        throw std::runtime_error("misplaced EXPAND_STDERR_PATH packet");

    if (child_options->expand_stderr_path != nullptr)
        throw std::runtime_error("duplicate EXPAND_STDERR_PATH packet");

    child_options->expand_stderr_path = path;
}

#endif

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

inline void
TranslateParser::HandleRefence(StringView payload)
{
    if (child_options == nullptr || !child_options->refence.IsEmpty())
        throw std::runtime_error("misplaced REFENCE packet");

    if (!CheckRefence(payload))
        throw std::runtime_error("malformed REFENCE packet");

    child_options->refence.Set(payload);
}

inline void
TranslateParser::HandleUidGid(ConstBuffer<void> _payload)
{
    if (child_options == nullptr || !child_options->uid_gid.IsEmpty())
        throw std::runtime_error("misplaced UID_GID packet");

    UidGid &uid_gid = child_options->uid_gid;

    constexpr size_t min_size = sizeof(int) * 2;
    const size_t max_size = min_size + sizeof(int) * uid_gid.groups.max_size();

    if (_payload.size < min_size || _payload.size > max_size ||
        _payload.size % sizeof(int) != 0)
        throw std::runtime_error("malformed UID_GID packet");

    const auto payload = ConstBuffer<int>::FromVoid(_payload);
    uid_gid.uid = payload[0];
    uid_gid.gid = payload[1];

    size_t n_groups = payload.size - 2;
    std::copy(std::next(payload.begin(), 2), payload.end(),
              uid_gid.groups.begin());
    if (n_groups < uid_gid.groups.max_size())
        uid_gid.groups[n_groups] = 0;
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

inline void
TranslateParser::HandleCgroupSet(StringView payload)
{
    if (child_options == nullptr)
        throw std::runtime_error("misplaced CGROUP_SET packet");

    auto set = ParseCgroupSet(payload);
    if (set.first.IsNull())
        throw std::runtime_error("malformed CGROUP_SET packet");

    child_options->cgroup.Set(alloc, set.first, set.second);
}

static bool
CheckProbeSuffix(const char *payload, size_t length)
{
    return memchr(payload, '/', length) == nullptr &&
        !has_null_byte(payload, length);
}

inline void
TranslateParser::HandleRegularPacket(enum beng_translation_command command,
                                     const void *const _payload,
                                     size_t payload_length)
{
    const char *const payload = (const char *)_payload;

    switch (command) {
#if TRANSLATION_ENABLE_TRANSFORMATION
        Transformation *new_transformation;
#endif

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
    case TRANSLATE_CRON:
    case TRANSLATE_PASSWORD:
    case TRANSLATE_SERVICE:
        throw std::runtime_error("misplaced translate request packet");

    case TRANSLATE_UID_GID:
        HandleUidGid({_payload, payload_length});
        return;

    case TRANSLATE_STATUS:
        if (payload_length != 2)
            throw std::runtime_error("size mismatch in STATUS packet from translation server");

#if TRANSLATION_ENABLE_HTTP
        response.status = http_status_t(*(const uint16_t*)(const void *)payload);

        if (!http_status_is_valid(response.status))
            throw FormatRuntimeError("invalid HTTP status code %u",
                                     response.status);
#else
        response.status = *(const uint16_t *)(const void *)payload;
#endif

        return;

    case TRANSLATE_PATH:
#if TRANSLATION_ENABLE_RADDRESS
        if (!is_valid_absolute_path(payload, payload_length))
            throw std::runtime_error("malformed PATH packet");

        if (nfs_address != nullptr && *nfs_address->path == 0) {
            nfs_address->path = payload;
            return;
        }

        if (resource_address == nullptr || resource_address->IsDefined())
            throw std::runtime_error("misplaced PATH packet");

        file_address = alloc.New<FileAddress>(payload);
        *resource_address = *file_address;
        return;
#else
        break;
#endif

    case TRANSLATE_PATH_INFO:
#if TRANSLATION_ENABLE_RADDRESS
        if (has_null_byte(payload, payload_length))
            throw std::runtime_error("malformed PATH_INFO packet");

        if (cgi_address != nullptr &&
            cgi_address->path_info == nullptr) {
            cgi_address->path_info = payload;
            return;
        } else if (file_address != nullptr) {
            /* don't emit an error when the resource is a local path.
               This combination might be useful one day, but isn't
               currently used. */
            return;
        } else
            throw std::runtime_error("misplaced PATH_INFO packet");
#else
        break;
#endif

    case TRANSLATE_EXPAND_PATH:
#if TRANSLATION_ENABLE_RADDRESS && TRANSLATION_ENABLE_EXPAND
        if (has_null_byte(payload, payload_length))
            throw std::runtime_error("malformed EXPAND_PATH packet");

        if (response.regex == nullptr) {
            throw std::runtime_error("misplaced EXPAND_PATH packet");
        } else if (cgi_address != nullptr &&
                   cgi_address->expand_path == nullptr) {
            cgi_address->expand_path = payload;
            return;
        } else if (nfs_address != nullptr &&
                   nfs_address->expand_path == nullptr) {
            nfs_address->expand_path = payload;
            return;
        } else if (file_address != nullptr &&
                   file_address->expand_path == nullptr) {
            file_address->expand_path = payload;
            return;
        } else if (http_address != NULL &&
                   http_address->expand_path == NULL) {
            http_address->expand_path = payload;
            return;
        } else
            throw std::runtime_error("misplaced EXPAND_PATH packet");
#else
        break;
#endif

    case TRANSLATE_EXPAND_PATH_INFO:
#if TRANSLATION_ENABLE_RADDRESS && TRANSLATION_ENABLE_EXPAND
        if (has_null_byte(payload, payload_length))
            throw std::runtime_error("malformed EXPAND_PATH_INFO packet");

        if (response.regex == nullptr) {
            throw std::runtime_error("misplaced EXPAND_PATH_INFO packet");
        } else if (cgi_address != nullptr &&
                   cgi_address->expand_path_info == nullptr) {
            cgi_address->expand_path_info = payload;
        } else if (file_address != nullptr) {
            /* don't emit an error when the resource is a local path.
               This combination might be useful one day, but isn't
               currently used. */
        } else
            throw std::runtime_error("misplaced EXPAND_PATH_INFO packet");

        return;
#else
        break;
#endif

    case TRANSLATE_DEFLATED:
#if TRANSLATION_ENABLE_RADDRESS
        if (!is_valid_absolute_path(payload, payload_length))
            throw std::runtime_error("malformed DEFLATED packet");

        if (file_address != nullptr) {
            file_address->deflated = payload;
            return;
        } else if (nfs_address != nullptr) {
            /* ignore for now */
        } else
            throw std::runtime_error("misplaced DEFLATED packet");
        return;
#else
        break;
#endif

    case TRANSLATE_GZIPPED:
#if TRANSLATION_ENABLE_RADDRESS
        if (!is_valid_absolute_path(payload, payload_length))
            throw std::runtime_error("malformed GZIPPED packet");

        if (file_address != nullptr) {
            if (file_address->auto_gzipped ||
                file_address->gzipped != nullptr)
                throw std::runtime_error("misplaced GZIPPED packet");

            file_address->gzipped = payload;
            return;
        } else if (nfs_address != nullptr) {
            /* ignore for now */
            return;
        } else {
            throw std::runtime_error("misplaced GZIPPED packet");
        }

        return;
#else
        break;
#endif

    case TRANSLATE_SITE:
#if TRANSLATION_ENABLE_RADDRESS
        assert(resource_address != nullptr);

        if (!is_valid_nonempty_string(payload, payload_length))
            throw std::runtime_error("malformed SITE packet");

        if (resource_address == &response.address)
            response.site = payload;
        else if (jail != nullptr && jail->enabled)
            jail->site_id = payload;
        else
            throw std::runtime_error("misplaced SITE packet");

        return;
#else
        break;
#endif

    case TRANSLATE_CONTENT_TYPE:
#if TRANSLATION_ENABLE_RADDRESS
        if (!is_valid_nonempty_string(payload, payload_length))
            throw std::runtime_error("malformed CONTENT_TYPE packet");

        if (file_address != nullptr) {
            if (!file_address->content_type_lookup.IsNull())
                throw std::runtime_error("CONTENT_TYPE/CONTENT_TYPE_LOOKUP conflict");

            file_address->content_type = payload;
        } else if (nfs_address != nullptr) {
            if (!nfs_address->content_type_lookup.IsNull())
                throw std::runtime_error("CONTENT_TYPE/CONTENT_TYPE_LOOKUP conflict");

            nfs_address->content_type = payload;
        } else if (from_request.content_type_lookup) {
            response.content_type = payload;
        } else
            throw std::runtime_error("misplaced CONTENT_TYPE packet");

        return;
#else
        break;
#endif

    case TRANSLATE_HTTP:
#if TRANSLATION_ENABLE_RADDRESS
        if (resource_address == nullptr || resource_address->IsDefined())
            throw std::runtime_error("misplaced HTTP packet");

        if (!is_valid_nonempty_string(payload, payload_length))
            throw std::runtime_error("malformed HTTP packet");

        http_address = http_address_parse(alloc, payload);
        if (http_address->protocol != HttpAddress::Protocol::HTTP)
            throw std::runtime_error("malformed HTTP packet");

        *resource_address = *http_address;

        address_list = &http_address->addresses;
        default_port = http_address->GetDefaultPort();
        return;
#else
        break;
#endif

    case TRANSLATE_REDIRECT:
#if TRANSLATION_ENABLE_HTTP
        if (!is_valid_nonempty_string(payload, payload_length))
            throw std::runtime_error("malformed REDIRECT packet");

        response.redirect = payload;
        return;
#else
        break;
#endif

    case TRANSLATE_EXPAND_REDIRECT:
#if TRANSLATION_ENABLE_HTTP && TRANSLATION_ENABLE_EXPAND
        if (response.regex == nullptr ||
            response.redirect == nullptr ||
            response.expand_redirect != nullptr)
            throw std::runtime_error("misplaced EXPAND_REDIRECT packet");

        if (!is_valid_nonempty_string(payload, payload_length))
            throw std::runtime_error("malformed EXPAND_REDIRECT packet");

        response.expand_redirect = payload;
        return;
#else
        break;
#endif

    case TRANSLATE_BOUNCE:
#if TRANSLATION_ENABLE_HTTP
        if (!is_valid_nonempty_string(payload, payload_length))
            throw std::runtime_error("malformed BOUNCE packet");

        response.bounce = payload;
        return;
#else
        break;
#endif

    case TRANSLATE_FILTER:
#if TRANSLATION_ENABLE_TRANSFORMATION
        resource_address = AddFilter();
        jail = nullptr;
        child_options = nullptr;
        ns_options = nullptr;
        mount_list = nullptr;
        file_address = nullptr;
        cgi_address = nullptr;
        nfs_address = nullptr;
        lhttp_address = nullptr;
        address_list = nullptr;
        return;
#else
        break;
#endif

    case TRANSLATE_FILTER_4XX:
#if TRANSLATION_ENABLE_TRANSFORMATION
        if (view != nullptr)
            view->filter_4xx = true;
        else
            response.filter_4xx = true;
        return;
#else
        break;
#endif

    case TRANSLATE_PROCESS:
#if TRANSLATION_ENABLE_TRANSFORMATION
        new_transformation = AddTransformation();
        new_transformation->type = Transformation::Type::PROCESS;
        new_transformation->u.processor.options = PROCESSOR_REWRITE_URL;
        return;
#else
        break;
#endif

    case TRANSLATE_DOMAIN:
        throw std::runtime_error("deprecated DOMAIN packet");

    case TRANSLATE_CONTAINER:
#if TRANSLATION_ENABLE_TRANSFORMATION
        if (transformation == nullptr ||
            transformation->type != Transformation::Type::PROCESS)
            throw std::runtime_error("misplaced CONTAINER packet");

        transformation->u.processor.options |= PROCESSOR_CONTAINER;
        return;
#else
        break;
#endif

    case TRANSLATE_SELF_CONTAINER:
#if TRANSLATION_ENABLE_TRANSFORMATION
        if (transformation == nullptr ||
            transformation->type != Transformation::Type::PROCESS)
            throw std::runtime_error("misplaced SELF_CONTAINER packet");

        transformation->u.processor.options |=
            PROCESSOR_SELF_CONTAINER|PROCESSOR_CONTAINER;
        return;
#else
        break;
#endif

    case TRANSLATE_GROUP_CONTAINER:
#if TRANSLATION_ENABLE_TRANSFORMATION
        if (!is_valid_nonempty_string(payload, payload_length))
            throw std::runtime_error("malformed GROUP_CONTAINER packet");

        if (transformation == nullptr ||
            transformation->type != Transformation::Type::PROCESS)
            throw std::runtime_error("misplaced GROUP_CONTAINER packet");

        transformation->u.processor.options |= PROCESSOR_CONTAINER;
        response.container_groups.Add(alloc, payload);
        return;
#else
        break;
#endif

    case TRANSLATE_WIDGET_GROUP:
#if TRANSLATION_ENABLE_WIDGET
        if (!is_valid_nonempty_string(payload, payload_length))
            throw std::runtime_error("malformed WIDGET_GROUP packet");

        response.widget_group = payload;
        return;
#else
        break;
#endif

    case TRANSLATE_UNTRUSTED:
#if TRANSLATION_ENABLE_WIDGET
        if (!is_valid_nonempty_string(payload, payload_length) || *payload == '.' ||
            payload[payload_length - 1] == '.')
            throw std::runtime_error("malformed UNTRUSTED packet");

        if (response.HasUntrusted())
            throw std::runtime_error("misplaced UNTRUSTED packet");

        response.untrusted = payload;
        return;
#else
        break;
#endif

    case TRANSLATE_UNTRUSTED_PREFIX:
#if TRANSLATION_ENABLE_HTTP
        if (!is_valid_nonempty_string(payload, payload_length) || *payload == '.' ||
            payload[payload_length - 1] == '.')
            throw std::runtime_error("malformed UNTRUSTED_PREFIX packet");

        if (response.HasUntrusted())
            throw std::runtime_error("misplaced UNTRUSTED_PREFIX packet");

        response.untrusted_prefix = payload;
        return;
#else
        break;
#endif

    case TRANSLATE_UNTRUSTED_SITE_SUFFIX:
#if TRANSLATION_ENABLE_HTTP
        if (!is_valid_nonempty_string(payload, payload_length) || *payload == '.' ||
            payload[payload_length - 1] == '.')
            throw std::runtime_error("malformed UNTRUSTED_SITE_SUFFIX packet");

        if (response.HasUntrusted())
            throw std::runtime_error("misplaced UNTRUSTED_SITE_SUFFIX packet");

        response.untrusted_site_suffix = payload;
        return;
#else
        break;
#endif

    case TRANSLATE_SCHEME:
#if TRANSLATION_ENABLE_HTTP
        if (strncmp(payload, "http", 4) != 0)
            throw std::runtime_error("misplaced SCHEME packet");

        response.scheme = payload;
        return;
#else
        break;
#endif

    case TRANSLATE_HOST:
#if TRANSLATION_ENABLE_HTTP
        response.host = payload;
        return;
#else
        break;
#endif

    case TRANSLATE_URI:
#if TRANSLATION_ENABLE_HTTP
        if (!is_valid_absolute_uri(payload, payload_length))
            throw std::runtime_error("malformed URI packet");

        response.uri = payload;
        return;
#else
        break;
#endif

    case TRANSLATE_DIRECT_ADDRESSING:
#if TRANSLATION_ENABLE_WIDGET
        response.direct_addressing = true;
#endif
        return;

    case TRANSLATE_STATEFUL:
#if TRANSLATION_ENABLE_SESSION
        response.stateful = true;
        return;
#else
        break;
#endif

    case TRANSLATE_SESSION:
#if TRANSLATION_ENABLE_SESSION
        response.session = { payload, payload_length };
        return;
#else
        break;
#endif

    case TRANSLATE_USER:
#if TRANSLATION_ENABLE_SESSION
        response.user = payload;
        previous_command = command;
        return;
#else
        break;
#endif

    case TRANSLATE_REALM:
#if TRANSLATION_ENABLE_SESSION
        if (payload_length > 0)
            throw std::runtime_error("malformed REALM packet");

        if (response.realm != nullptr)
            throw std::runtime_error("duplicate REALM packet");

        if (response.realm_from_auth_base)
            throw std::runtime_error("misplaced REALM packet");

        response.realm = payload;
        return;
#else
        break;
#endif

    case TRANSLATE_LANGUAGE:
#if TRANSLATION_ENABLE_SESSION
        response.language = payload;
        return;
#else
        break;
#endif

    case TRANSLATE_PIPE:
#if TRANSLATION_ENABLE_RADDRESS
        if (resource_address == nullptr || resource_address->IsDefined())
            throw std::runtime_error("misplaced PIPE packet");

        if (payload_length == 0)
            throw std::runtime_error("malformed PIPE packet");

        SetCgiAddress(ResourceAddress::Type::PIPE, payload);
        return;
#else
        break;
#endif

    case TRANSLATE_CGI:
#if TRANSLATION_ENABLE_RADDRESS
        if (resource_address == nullptr || resource_address->IsDefined())
            throw std::runtime_error("misplaced CGI packet");

        if (!is_valid_absolute_path(payload, payload_length))
            throw std::runtime_error("malformed CGI packet");

        SetCgiAddress(ResourceAddress::Type::CGI, payload);
        cgi_address->document_root = response.document_root;
        return;
#else
        break;
#endif

    case TRANSLATE_FASTCGI:
#if TRANSLATION_ENABLE_RADDRESS
        if (resource_address == nullptr || resource_address->IsDefined())
            throw std::runtime_error("misplaced FASTCGI packet");

        if (!is_valid_absolute_path(payload, payload_length))
            throw std::runtime_error("malformed FASTCGI packet");

        SetCgiAddress(ResourceAddress::Type::FASTCGI, payload);
        address_list = &cgi_address->address_list;
        default_port = 9000;
        return;
#else
        break;
#endif

    case TRANSLATE_AJP:
#if TRANSLATION_ENABLE_RADDRESS
        if (resource_address == nullptr || resource_address->IsDefined())
            throw std::runtime_error("misplaced AJP packet");

        if (payload_length == 0)
            throw std::runtime_error("malformed AJP packet");

        http_address = http_address_parse(alloc, payload);
        if (http_address->protocol != HttpAddress::Protocol::AJP)
            throw std::runtime_error("malformed AJP packet");

        *resource_address = *http_address;

        address_list = &http_address->addresses;
        default_port = 8009;
        return;
#else
        break;
#endif

    case TRANSLATE_NFS_SERVER:
#if TRANSLATION_ENABLE_RADDRESS
        if (resource_address == nullptr || resource_address->IsDefined())
            throw std::runtime_error("misplaced NFS_SERVER packet");

        if (payload_length == 0)
            throw std::runtime_error("malformed NFS_SERVER packet");

        nfs_address = alloc.New<NfsAddress>(payload, "", "");
        *resource_address = *nfs_address;
        return;
#else
        break;
#endif

    case TRANSLATE_NFS_EXPORT:
#if TRANSLATION_ENABLE_RADDRESS
        if (nfs_address == nullptr ||
            *nfs_address->export_name != 0)
            throw std::runtime_error("misplaced NFS_EXPORT packet");

        if (!is_valid_absolute_path(payload, payload_length))
            throw std::runtime_error("malformed NFS_EXPORT packet");

        nfs_address->export_name = payload;
        return;
#else
        break;
#endif

    case TRANSLATE_JAILCGI:
#if TRANSLATION_ENABLE_JAILCGI
        if (jail == nullptr) {
            if (child_options == nullptr)
                throw std::runtime_error("misplaced JAILCGI packet");

            jail = child_options->jail = alloc.New<JailParams>();
        }

        jail->enabled = true;
        return;
#endif

    case TRANSLATE_HOME:
        translate_client_home(ns_options,
#if TRANSLATION_ENABLE_JAILCGI
                              jail,
#endif
                              payload, payload_length);
        return;

    case TRANSLATE_INTERPRETER:
#if TRANSLATION_ENABLE_RADDRESS
        if (resource_address == nullptr ||
            (resource_address->type != ResourceAddress::Type::CGI &&
             resource_address->type != ResourceAddress::Type::FASTCGI) ||
            cgi_address->interpreter != nullptr)
            throw std::runtime_error("misplaced INTERPRETER packet");

        cgi_address->interpreter = payload;
        return;
#else
        break;
#endif

    case TRANSLATE_ACTION:
#if TRANSLATION_ENABLE_RADDRESS
        if (resource_address == nullptr ||
            (resource_address->type != ResourceAddress::Type::CGI &&
             resource_address->type != ResourceAddress::Type::FASTCGI) ||
            cgi_address->action != nullptr)
            throw std::runtime_error("misplaced ACTION packet");

        cgi_address->action = payload;
        return;
#else
        break;
#endif

    case TRANSLATE_SCRIPT_NAME:
#if TRANSLATION_ENABLE_RADDRESS
        if (resource_address == nullptr ||
            (resource_address->type != ResourceAddress::Type::CGI &&
             resource_address->type != ResourceAddress::Type::WAS &&
             resource_address->type != ResourceAddress::Type::FASTCGI) ||
            cgi_address->script_name != nullptr)
            throw std::runtime_error("misplaced SCRIPT_NAME packet");

        cgi_address->script_name = payload;
        return;
#else
        break;
#endif

    case TRANSLATE_EXPAND_SCRIPT_NAME:
#if TRANSLATION_ENABLE_RADDRESS && TRANSLATION_ENABLE_EXPAND
        if (!is_valid_nonempty_string(payload, payload_length))
            throw std::runtime_error("malformed EXPAND_SCRIPT_NAME packet");

        if (response.regex == nullptr ||
            cgi_address == nullptr ||
            cgi_address->expand_script_name != nullptr)
            throw std::runtime_error("misplaced EXPAND_SCRIPT_NAME packet");

        cgi_address->expand_script_name = payload;
        return;
#else
        break;
#endif

    case TRANSLATE_DOCUMENT_ROOT:
#if TRANSLATION_ENABLE_RADDRESS
        if (!is_valid_absolute_path(payload, payload_length))
            throw std::runtime_error("malformed DOCUMENT_ROOT packet");

        if (cgi_address != nullptr)
            cgi_address->document_root = payload;
        else if (file_address != nullptr &&
                 file_address->delegate != nullptr)
            file_address->document_root = payload;
        else
            response.document_root = payload;
        return;
#else
        break;
#endif

    case TRANSLATE_EXPAND_DOCUMENT_ROOT:
#if TRANSLATION_ENABLE_RADDRESS && TRANSLATION_ENABLE_EXPAND
        if (!is_valid_nonempty_string(payload, payload_length))
            throw std::runtime_error("malformed EXPAND_DOCUMENT_ROOT packet");

        if (response.regex == nullptr)
            throw std::runtime_error("misplaced EXPAND_DOCUMENT_ROOT packet");

        if (cgi_address != nullptr)
            cgi_address->expand_document_root = payload;
        else if (file_address != nullptr &&
                 file_address->delegate != nullptr)
            file_address->expand_document_root = payload;
        else
            response.expand_document_root = payload;
        return;
#else
        break;
#endif

    case TRANSLATE_ADDRESS:
#if TRANSLATION_ENABLE_HTTP
        if (address_list == nullptr)
            throw std::runtime_error("misplaced ADDRESS packet");

        if (payload_length < 2)
            throw std::runtime_error("malformed ADDRESS packet");

        address_list->Add(alloc,
                          SocketAddress((const struct sockaddr *)_payload,
                                        payload_length));
        return;
#else
        break;
#endif

    case TRANSLATE_ADDRESS_STRING:
#if TRANSLATION_ENABLE_HTTP
        if (address_list == nullptr)
            throw std::runtime_error("misplaced ADDRESS_STRING packet");

        if (payload_length == 0)
            throw std::runtime_error("malformed ADDRESS_STRING packet");

        try {
            parse_address_string(alloc, address_list,
                                 payload, default_port);
        } catch (const std::exception &e) {
            throw FormatRuntimeError("malformed ADDRESS_STRING packet: %s",
                                     e.what());
        }

        return;
#else
        break;
#endif

    case TRANSLATE_VIEW:
#if TRANSLATION_ENABLE_WIDGET
        if (!valid_view_name(payload))
            throw std::runtime_error("invalid view name");

        AddView(payload);
        return;
#else
        break;
#endif

    case TRANSLATE_MAX_AGE:
        if (payload_length != 4)
            throw std::runtime_error("malformed MAX_AGE packet");

        switch (previous_command) {
        case TRANSLATE_BEGIN:
            response.max_age = std::chrono::seconds(*(const uint32_t *)_payload);
            break;

#if TRANSLATION_ENABLE_SESSION
        case TRANSLATE_USER:
            response.user_max_age = std::chrono::seconds(*(const uint32_t *)_payload);
            break;
#endif

        default:
            throw std::runtime_error("misplaced MAX_AGE packet");
        }

        return;

    case TRANSLATE_VARY:
#if TRANSLATION_ENABLE_CACHE
        if (payload_length == 0 ||
            payload_length % sizeof(response.vary.data[0]) != 0)
            throw std::runtime_error("malformed VARY packet");

        response.vary.data = (const uint16_t *)_payload;
        response.vary.size = payload_length / sizeof(response.vary.data[0]);
        return;
#else
        break;
#endif

    case TRANSLATE_INVALIDATE:
#if TRANSLATION_ENABLE_CACHE
        if (payload_length == 0 ||
            payload_length % sizeof(response.invalidate.data[0]) != 0)
            throw std::runtime_error("malformed INVALIDATE packet");

        response.invalidate.data = (const uint16_t *)_payload;
        response.invalidate.size = payload_length /
            sizeof(response.invalidate.data[0]);
        return;
#else
        break;
#endif

    case TRANSLATE_BASE:
#if TRANSLATION_ENABLE_RADDRESS
        if (!is_valid_absolute_uri(payload, payload_length) ||
            payload[payload_length - 1] != '/')
            throw std::runtime_error("malformed BASE packet");

        if (from_request.uri == nullptr ||
            response.auto_base ||
            response.base != nullptr)
            throw std::runtime_error("misplaced BASE packet");

        if (memcmp(from_request.uri, payload, payload_length) != 0)
            throw std::runtime_error("BASE mismatches request URI");

        response.base = payload;
        return;
#else
        break;
#endif

    case TRANSLATE_UNSAFE_BASE:
#if TRANSLATION_ENABLE_RADDRESS
        if (payload_length > 0)
            throw std::runtime_error("malformed UNSAFE_BASE packet");

        if (response.base == nullptr)
            throw std::runtime_error("misplaced UNSAFE_BASE packet");

        response.unsafe_base = true;
        return;
#else
        break;
#endif

    case TRANSLATE_EASY_BASE:
#if TRANSLATION_ENABLE_RADDRESS
        if (payload_length > 0)
            throw std::runtime_error("malformed EASY_BASE");

        if (response.base == nullptr)
            throw std::runtime_error("EASY_BASE without BASE");

        if (response.easy_base)
            throw std::runtime_error("duplicate EASY_BASE");

        response.easy_base = true;
        return;
#else
        break;
#endif

    case TRANSLATE_REGEX:
#if TRANSLATION_ENABLE_EXPAND
        if (response.base == nullptr)
            throw std::runtime_error("REGEX without BASE");

        if (response.regex != nullptr)
            throw std::runtime_error("duplicate REGEX");

        if (!is_valid_nonempty_string(payload, payload_length))
            throw std::runtime_error("malformed REGEX packet");

        response.regex = payload;
        return;
#else
        break;
#endif

    case TRANSLATE_INVERSE_REGEX:
#if TRANSLATION_ENABLE_EXPAND
        if (response.base == nullptr)
            throw std::runtime_error("INVERSE_REGEX without BASE");

        if (response.inverse_regex != nullptr)
            throw std::runtime_error("duplicate INVERSE_REGEX");

        if (!is_valid_nonempty_string(payload, payload_length))
            throw std::runtime_error("malformed INVERSE_REGEX packet");

        response.inverse_regex = payload;
        return;
#else
        break;
#endif

    case TRANSLATE_REGEX_TAIL:
#if TRANSLATION_ENABLE_EXPAND
        if (payload_length > 0)
            throw std::runtime_error("malformed REGEX_TAIL packet");

        if (response.regex == nullptr && response.inverse_regex == nullptr)
            throw std::runtime_error("misplaced REGEX_TAIL packet");

        if (response.regex_tail)
            throw std::runtime_error("duplicate REGEX_TAIL packet");

        response.regex_tail = true;
        return;
#else
        break;
#endif

    case TRANSLATE_REGEX_UNESCAPE:
#if TRANSLATION_ENABLE_EXPAND
        if (payload_length > 0)
            throw std::runtime_error("malformed REGEX_UNESCAPE packet");

        if (response.regex == nullptr && response.inverse_regex == nullptr)
            throw std::runtime_error("misplaced REGEX_UNESCAPE packet");

        if (response.regex_unescape)
            throw std::runtime_error("duplicate REGEX_UNESCAPE packet");

        response.regex_unescape = true;
        return;
#else
        break;
#endif

    case TRANSLATE_DELEGATE:
#if TRANSLATION_ENABLE_RADDRESS
        if (file_address == nullptr)
            throw std::runtime_error("misplaced DELEGATE packet");

        if (!is_valid_absolute_path(payload, payload_length))
            throw std::runtime_error("malformed DELEGATE packet");

        file_address->delegate = alloc.New<DelegateAddress>(payload);
        SetChildOptions(file_address->delegate->child_options);
        return;
#else
        break;
#endif

    case TRANSLATE_APPEND:
        if (!is_valid_nonempty_string(payload, payload_length))
            throw std::runtime_error("malformed APPEND packet");

        if (!HasArgs())
            throw std::runtime_error("misplaced APPEND packet");

        args_builder.Add(alloc, payload, false);
        return;

    case TRANSLATE_EXPAND_APPEND:
#if TRANSLATION_ENABLE_EXPAND
        if (!is_valid_nonempty_string(payload, payload_length))
            throw std::runtime_error("malformed EXPAND_APPEND packet");

        if (response.regex == nullptr || !HasArgs() ||
            !args_builder.CanSetExpand())
            throw std::runtime_error("misplaced EXPAND_APPEND packet");

        args_builder.SetExpand(payload);
        return;
#else
        break;
#endif

    case TRANSLATE_PAIR:
#if TRANSLATION_ENABLE_RADDRESS
        if (cgi_address != nullptr &&
            resource_address->type != ResourceAddress::Type::CGI &&
            resource_address->type != ResourceAddress::Type::PIPE) {
            translate_client_pair(alloc, params_builder, "PAIR",
                                  payload, payload_length);
            return;
        }
#endif

        if (child_options != nullptr) {
            translate_client_pair(alloc, env_builder, "PAIR",
                                  payload, payload_length);
        } else
            throw std::runtime_error("misplaced PAIR packet");
        return;

    case TRANSLATE_EXPAND_PAIR:
#if TRANSLATION_ENABLE_RADDRESS
        if (response.regex == nullptr)
            throw std::runtime_error("misplaced EXPAND_PAIR packet");

        if (cgi_address != nullptr) {
            const auto type = resource_address->type;
            auto &builder = type == ResourceAddress::Type::CGI
                ? env_builder
                : params_builder;

            translate_client_expand_pair(builder, "EXPAND_PAIR",
                                         payload, payload_length);
        } else if (lhttp_address != nullptr) {
            translate_client_expand_pair(env_builder,
                                         "EXPAND_PAIR",
                                         payload, payload_length);
        } else
            throw std::runtime_error("misplaced EXPAND_PAIR packet");
        return;
#else
        break;
#endif

    case TRANSLATE_DISCARD_SESSION:
#if TRANSLATION_ENABLE_SESSION
        response.discard_session = true;
        return;
#else
        break;
#endif

    case TRANSLATE_REQUEST_HEADER_FORWARD:
#if TRANSLATION_ENABLE_HTTP
        if (view != nullptr)
            parse_header_forward(&view->request_header_forward,
                                 payload, payload_length);
        else
            parse_header_forward(&response.request_header_forward,
                                 payload, payload_length);
        return;
#else
        break;
#endif

    case TRANSLATE_RESPONSE_HEADER_FORWARD:
#if TRANSLATION_ENABLE_HTTP
        if (view != nullptr)
            parse_header_forward(&view->response_header_forward,
                                 payload, payload_length);
        else
            parse_header_forward(&response.response_header_forward,
                                 payload, payload_length);
        return;
#else
        break;
#endif

    case TRANSLATE_WWW_AUTHENTICATE:
#if TRANSLATION_ENABLE_SESSION
        if (!is_valid_nonempty_string(payload, payload_length))
            throw std::runtime_error("malformed WWW_AUTHENTICATE packet");

        response.www_authenticate = payload;
        return;
#else
        break;
#endif

    case TRANSLATE_AUTHENTICATION_INFO:
#if TRANSLATION_ENABLE_SESSION
        if (!is_valid_nonempty_string(payload, payload_length))
            throw std::runtime_error("malformed AUTHENTICATION_INFO packet");

        response.authentication_info = payload;
        return;
#else
        break;
#endif

    case TRANSLATE_HEADER:
#if TRANSLATION_ENABLE_HTTP
        parse_header(alloc, response.response_headers,
                     "HEADER", payload, payload_length);
        return;
#else
        break;
#endif

    case TRANSLATE_SECURE_COOKIE:
#if TRANSLATION_ENABLE_SESSION
        response.secure_cookie = true;
        return;
#else
        break;
#endif

    case TRANSLATE_COOKIE_DOMAIN:
#if TRANSLATION_ENABLE_SESSION
        if (response.cookie_domain != nullptr)
            throw std::runtime_error("misplaced COOKIE_DOMAIN packet");

        if (!is_valid_nonempty_string(payload, payload_length))
            throw std::runtime_error("malformed COOKIE_DOMAIN packet");

        response.cookie_domain = payload;
        return;
#else
        break;
#endif

    case TRANSLATE_ERROR_DOCUMENT:
        response.error_document = { payload, payload_length };
        return;

    case TRANSLATE_CHECK:
#if TRANSLATION_ENABLE_SESSION
        if (!response.check.IsNull())
            throw std::runtime_error("duplicate CHECK packet");

        response.check = { payload, payload_length };
        return;
#else
        break;
#endif

    case TRANSLATE_PREVIOUS:
        response.previous = true;
        return;

    case TRANSLATE_WAS:
#if TRANSLATION_ENABLE_RADDRESS
        if (resource_address == nullptr || resource_address->IsDefined())
            throw std::runtime_error("misplaced WAS packet");

        if (!is_valid_absolute_path(payload, payload_length))
            throw std::runtime_error("malformed WAS packet");

        SetCgiAddress(ResourceAddress::Type::WAS, payload);
        return;
#else
        break;
#endif

    case TRANSLATE_TRANSPARENT:
        response.transparent = true;
        return;

    case TRANSLATE_WIDGET_INFO:
#if TRANSLATION_ENABLE_WIDGET
        response.widget_info = true;
#endif
        return;

    case TRANSLATE_STICKY:
#if TRANSLATION_ENABLE_RADDRESS
        if (address_list == nullptr)
            throw std::runtime_error("misplaced STICKY packet");

        address_list->SetStickyMode(StickyMode::SESSION_MODULO);
        return;
#else
        break;
#endif

    case TRANSLATE_DUMP_HEADERS:
#if TRANSLATION_ENABLE_HTTP
        response.dump_headers = true;
#endif
        return;

    case TRANSLATE_COOKIE_HOST:
#if TRANSLATION_ENABLE_SESSION
        if (resource_address == nullptr || !resource_address->IsDefined())
            throw std::runtime_error("misplaced COOKIE_HOST packet");

        if (!is_valid_nonempty_string(payload, payload_length))
            throw std::runtime_error("malformed COOKIE_HOST packet");

        response.cookie_host = payload;
        return;
#else
        break;
#endif

    case TRANSLATE_COOKIE_PATH:
#if TRANSLATION_ENABLE_SESSION
        if (response.cookie_path != nullptr)
            throw std::runtime_error("misplaced COOKIE_PATH packet");

        if (!is_valid_absolute_uri(payload, payload_length))
            throw std::runtime_error("malformed COOKIE_PATH packet");

        response.cookie_path = payload;
        return;
#else
        break;
#endif

    case TRANSLATE_PROCESS_CSS:
#if TRANSLATION_ENABLE_TRANSFORMATION
        new_transformation = AddTransformation();
        new_transformation->type = Transformation::Type::PROCESS_CSS;
        new_transformation->u.css_processor.options = CSS_PROCESSOR_REWRITE_URL;
        return;
#else
        break;
#endif

    case TRANSLATE_PREFIX_CSS_CLASS:
#if TRANSLATION_ENABLE_TRANSFORMATION
        if (transformation == nullptr)
            throw std::runtime_error("misplaced PREFIX_CSS_CLASS packet");

        switch (transformation->type) {
        case Transformation::Type::PROCESS:
            transformation->u.processor.options |= PROCESSOR_PREFIX_CSS_CLASS;
            break;

        case Transformation::Type::PROCESS_CSS:
            transformation->u.css_processor.options |= CSS_PROCESSOR_PREFIX_CLASS;
            break;

        default:
            throw std::runtime_error("misplaced PREFIX_CSS_CLASS packet");
        }

        return;
#else
        break;
#endif

    case TRANSLATE_PREFIX_XML_ID:
#if TRANSLATION_ENABLE_TRANSFORMATION
        if (transformation == nullptr)
            throw std::runtime_error("misplaced PREFIX_XML_ID packet");

        switch (transformation->type) {
        case Transformation::Type::PROCESS:
            transformation->u.processor.options |= PROCESSOR_PREFIX_XML_ID;
            break;

        case Transformation::Type::PROCESS_CSS:
            transformation->u.css_processor.options |= CSS_PROCESSOR_PREFIX_ID;
            break;

        default:
            throw std::runtime_error("misplaced PREFIX_XML_ID packet");
        }

        return;
#else
        break;
#endif

    case TRANSLATE_PROCESS_STYLE:
#if TRANSLATION_ENABLE_TRANSFORMATION
        if (transformation == nullptr ||
            transformation->type != Transformation::Type::PROCESS)
            throw std::runtime_error("misplaced PROCESS_STYLE packet");

        transformation->u.processor.options |= PROCESSOR_STYLE;
        return;
#else
        break;
#endif

    case TRANSLATE_FOCUS_WIDGET:
#if TRANSLATION_ENABLE_TRANSFORMATION
        if (transformation == nullptr ||
            transformation->type != Transformation::Type::PROCESS)
            throw std::runtime_error("misplaced FOCUS_WIDGET packet");

        transformation->u.processor.options |= PROCESSOR_FOCUS_WIDGET;
        return;
#else
        break;
#endif

    case TRANSLATE_ANCHOR_ABSOLUTE:
#if TRANSLATION_ENABLE_WIDGET
        if (transformation == nullptr ||
            transformation->type != Transformation::Type::PROCESS)
            throw std::runtime_error("misplaced ANCHOR_ABSOLUTE packet");

        response.anchor_absolute = true;
        return;
#else
        break;
#endif

    case TRANSLATE_PROCESS_TEXT:
#if TRANSLATION_ENABLE_TRANSFORMATION
        new_transformation = AddTransformation();
        new_transformation->type = Transformation::Type::PROCESS_TEXT;
        return;
#else
        break;
#endif

    case TRANSLATE_LOCAL_URI:
#if TRANSLATION_ENABLE_HTTP
        if (response.local_uri != nullptr)
            throw std::runtime_error("misplaced LOCAL_URI packet");

        if (payload_length == 0 || payload[payload_length - 1] != '/')
            throw std::runtime_error("malformed LOCAL_URI packet");

        response.local_uri = payload;
        return;
#else
        break;
#endif

    case TRANSLATE_AUTO_BASE:
#if TRANSLATION_ENABLE_RADDRESS
        if (resource_address != &response.address ||
            cgi_address == nullptr ||
            cgi_address != &response.address.GetCgi() ||
            cgi_address->path_info == nullptr ||
            from_request.uri == nullptr ||
            response.base != nullptr ||
            response.auto_base)
            throw std::runtime_error("misplaced AUTO_BASE packet");

        response.auto_base = true;
        return;
#else
        break;
#endif

    case TRANSLATE_VALIDATE_MTIME:
        if (payload_length < 10 || payload[8] != '/' ||
            memchr(payload + 9, 0, payload_length - 9) != nullptr)
            throw std::runtime_error("malformed VALIDATE_MTIME packet");

        response.validate_mtime.mtime = *(const uint64_t *)_payload;
        response.validate_mtime.path =
            alloc.DupZ({payload + 8, payload_length - 8});
        return;

    case TRANSLATE_LHTTP_PATH:
#if TRANSLATION_ENABLE_RADDRESS
        if (resource_address == nullptr || resource_address->IsDefined())
            throw std::runtime_error("misplaced LHTTP_PATH packet");

        if (!is_valid_absolute_path(payload, payload_length))
            throw std::runtime_error("malformed LHTTP_PATH packet");

        lhttp_address = alloc.New<LhttpAddress>(payload);
        *resource_address = *lhttp_address;

        args_builder = lhttp_address->args;
        SetChildOptions(lhttp_address->options);
        return;
#else
        break;
#endif

    case TRANSLATE_LHTTP_URI:
#if TRANSLATION_ENABLE_RADDRESS
        if (lhttp_address == nullptr ||
            lhttp_address->uri != nullptr)
            throw std::runtime_error("misplaced LHTTP_HOST packet");

        if (!is_valid_absolute_uri(payload, payload_length))
            throw std::runtime_error("malformed LHTTP_URI packet");

        lhttp_address->uri = payload;
        return;
#else
        break;
#endif

    case TRANSLATE_EXPAND_LHTTP_URI:
#if TRANSLATION_ENABLE_RADDRESS
        if (lhttp_address == nullptr ||
            lhttp_address->uri == nullptr ||
            lhttp_address->expand_uri != nullptr ||
            response.regex == nullptr)
            throw std::runtime_error("misplaced EXPAND_LHTTP_URI packet");

        if (!is_valid_nonempty_string(payload, payload_length))
            throw std::runtime_error("malformed EXPAND_LHTTP_URI packet");

        lhttp_address->expand_uri = payload;
        return;
#else
        break;
#endif

    case TRANSLATE_LHTTP_HOST:
#if TRANSLATION_ENABLE_RADDRESS
        if (lhttp_address == nullptr ||
            lhttp_address->host_and_port != nullptr)
            throw std::runtime_error("misplaced LHTTP_HOST packet");

        if (!is_valid_nonempty_string(payload, payload_length))
            throw std::runtime_error("malformed LHTTP_HOST packet");

        lhttp_address->host_and_port = payload;
        return;
#else
        break;
#endif

    case TRANSLATE_CONCURRENCY:
#if TRANSLATION_ENABLE_RADDRESS
        if (lhttp_address == nullptr)
            throw std::runtime_error("misplaced CONCURRENCY packet");

        if (payload_length != 2)
            throw std::runtime_error("malformed CONCURRENCY packet");

        lhttp_address->concurrency = *(const uint16_t *)_payload;
        return;
#else
        break;
#endif

    case TRANSLATE_WANT_FULL_URI:
#if TRANSLATION_ENABLE_HTTP
        if (from_request.want_full_uri)
            throw std::runtime_error("WANT_FULL_URI loop");

        if (!response.want_full_uri.IsNull())
            throw std::runtime_error("duplicate WANT_FULL_URI packet");

        response.want_full_uri = { payload, payload_length };
        return;
#else
        break;
#endif

    case TRANSLATE_USER_NAMESPACE:
        if (payload_length != 0)
            throw std::runtime_error("malformed USER_NAMESPACE packet");

        if (ns_options != nullptr) {
            ns_options->enable_user = true;
        } else
            throw std::runtime_error("misplaced USER_NAMESPACE packet");

        return;

    case TRANSLATE_PID_NAMESPACE:
        if (payload_length != 0)
            throw std::runtime_error("malformed PID_NAMESPACE packet");

        if (ns_options != nullptr) {
            ns_options->enable_pid = true;
        } else
            throw std::runtime_error("misplaced PID_NAMESPACE packet");

        return;

    case TRANSLATE_NETWORK_NAMESPACE:
        if (payload_length != 0)
            throw std::runtime_error("malformed NETWORK_NAMESPACE packet");

        if (ns_options != nullptr) {
            ns_options->enable_network = true;
        } else
            throw std::runtime_error("misplaced NETWORK_NAMESPACE packet");

        return;

    case TRANSLATE_PIVOT_ROOT:
        translate_client_pivot_root(ns_options, payload, payload_length);
        return;

    case TRANSLATE_MOUNT_PROC:
        translate_client_mount_proc(ns_options, payload_length);
        return;

    case TRANSLATE_MOUNT_HOME:
        translate_client_mount_home(ns_options, payload, payload_length);
        return;

    case TRANSLATE_BIND_MOUNT:
        HandleBindMount(payload, payload_length, false, false);
        return;

    case TRANSLATE_MOUNT_TMP_TMPFS:
        translate_client_mount_tmp_tmpfs(ns_options,
                                         { payload, payload_length });
        return;

    case TRANSLATE_UTS_NAMESPACE:
        translate_client_uts_namespace(ns_options, payload);
        return;

    case TRANSLATE_RLIMITS:
        translate_client_rlimits(alloc, child_options, payload);
        return;

    case TRANSLATE_WANT:
        HandleWant((const uint16_t *)_payload, payload_length);
        return;

    case TRANSLATE_FILE_NOT_FOUND:
#if TRANSLATION_ENABLE_RADDRESS
        translate_client_file_not_found(response,
                                        { _payload, payload_length });
        return;
#else
        break;
#endif

    case TRANSLATE_CONTENT_TYPE_LOOKUP:
#if TRANSLATION_ENABLE_RADDRESS
        HandleContentTypeLookup({ _payload, payload_length });
        return;
#else
        break;
#endif

    case TRANSLATE_DIRECTORY_INDEX:
#if TRANSLATION_ENABLE_RADDRESS
        translate_client_directory_index(response,
                                         { _payload, payload_length });
        return;
#else
        break;
#endif

    case TRANSLATE_EXPIRES_RELATIVE:
        translate_client_expires_relative(response,
                                          { _payload, payload_length });
        return;


    case TRANSLATE_TEST_PATH:
        if (!is_valid_absolute_path(payload, payload_length))
            throw std::runtime_error("malformed TEST_PATH packet");

        if (response.test_path != nullptr)
            throw std::runtime_error("duplicate TEST_PATH packet");

        response.test_path = payload;
        return;

    case TRANSLATE_EXPAND_TEST_PATH:
#if TRANSLATION_ENABLE_EXPAND
        if (response.regex == nullptr)
            throw std::runtime_error("misplaced EXPAND_TEST_PATH packet");

        if (!is_valid_nonempty_string(payload, payload_length))
            throw std::runtime_error("malformed EXPAND_TEST_PATH packet");

        if (response.expand_test_path != nullptr)
            throw std::runtime_error("duplicate EXPAND_TEST_PATH packet");

        response.expand_test_path = payload;
        return;
#else
        break;
#endif

    case TRANSLATE_REDIRECT_QUERY_STRING:
#if TRANSLATION_ENABLE_HTTP
        if (payload_length != 0)
            throw std::runtime_error("malformed REDIRECT_QUERY_STRING packet");

        if (response.redirect_query_string ||
            (response.redirect == nullptr &&
             response.expand_redirect == nullptr))
            throw std::runtime_error("misplaced REDIRECT_QUERY_STRING packet");

        response.redirect_query_string = true;
        return;
#else
        break;
#endif

    case TRANSLATE_ENOTDIR:
#if TRANSLATION_ENABLE_RADDRESS
        translate_client_enotdir(response, { _payload, payload_length });
        return;
#else
        break;
#endif

    case TRANSLATE_STDERR_PATH:
        translate_client_stderr_path(child_options,
                                     { _payload, payload_length });
        return;

    case TRANSLATE_AUTH:
#if TRANSLATION_ENABLE_SESSION
        if (response.HasAuth())
            throw std::runtime_error("duplicate AUTH packet");

        response.auth = { payload, payload_length };
        return;
#else
        break;
#endif

    case TRANSLATE_SETENV:
        if (child_options != nullptr) {
            translate_client_pair(alloc, env_builder,
                                  "SETENV",
                                  payload, payload_length);
        } else
            throw std::runtime_error("misplaced SETENV packet");
        return;

    case TRANSLATE_EXPAND_SETENV:
#if TRANSLATION_ENABLE_EXPAND
        if (response.regex == nullptr)
            throw std::runtime_error("misplaced EXPAND_SETENV packet");

        if (child_options != nullptr) {
            translate_client_expand_pair(env_builder,
                                         "EXPAND_SETENV",
                                         payload, payload_length);
        } else
            throw std::runtime_error("misplaced SETENV packet");
        return;
#else
        break;
#endif

    case TRANSLATE_EXPAND_URI:
#if TRANSLATION_ENABLE_EXPAND
        if (response.regex == nullptr ||
            response.uri == nullptr ||
            response.expand_uri != nullptr)
            throw std::runtime_error("misplaced EXPAND_URI packet");

        if (!is_valid_nonempty_string(payload, payload_length))
            throw std::runtime_error("malformed EXPAND_URI packet");

        response.expand_uri = payload;
        return;
#else
        break;
#endif

    case TRANSLATE_EXPAND_SITE:
#if TRANSLATION_ENABLE_EXPAND
        if (response.regex == nullptr ||
            response.site == nullptr ||
            response.expand_site != nullptr)
            throw std::runtime_error("misplaced EXPAND_SITE packet");

        if (!is_valid_nonempty_string(payload, payload_length))
            throw std::runtime_error("malformed EXPAND_SITE packet");

        response.expand_site = payload;
        return;
#endif

    case TRANSLATE_REQUEST_HEADER:
#if TRANSLATION_ENABLE_HTTP
        parse_header(alloc, response.request_headers,
                     "REQUEST_HEADER", payload, payload_length);
        return;
#else
        break;
#endif

    case TRANSLATE_EXPAND_REQUEST_HEADER:
#if TRANSLATION_ENABLE_HTTP && TRANSLATION_ENABLE_EXPAND
        if (response.regex == nullptr)
            throw std::runtime_error("misplaced EXPAND_REQUEST_HEADERS packet");

        parse_header(alloc,
                     response.expand_request_headers,
                     "EXPAND_REQUEST_HEADER", payload, payload_length);
        return;
#else
        break;
#endif

    case TRANSLATE_AUTO_GZIPPED:
#if TRANSLATION_ENABLE_EXPAND
        if (payload_length > 0)
            throw std::runtime_error("malformed AUTO_GZIPPED packet");

        if (file_address != nullptr) {
            if (file_address->auto_gzipped ||
                file_address->gzipped != nullptr)
                throw std::runtime_error("misplaced AUTO_GZIPPED packet");

            file_address->auto_gzipped = true;
        } else if (nfs_address != nullptr) {
            /* ignore for now */
        } else
            throw std::runtime_error("misplaced AUTO_GZIPPED packet");
#endif
        return;

    case TRANSLATE_PROBE_PATH_SUFFIXES:
        if (!response.probe_path_suffixes.IsNull() ||
            (response.test_path == nullptr &&
             response.expand_test_path == nullptr))
            throw std::runtime_error("misplaced PROBE_PATH_SUFFIXES packet");

        response.probe_path_suffixes = { payload, payload_length };
        return;

    case TRANSLATE_PROBE_SUFFIX:
        if (response.probe_path_suffixes.IsNull())
            throw std::runtime_error("misplaced PROBE_SUFFIX packet");

        if (response.probe_suffixes.full())
            throw std::runtime_error("too many PROBE_SUFFIX packets");

        if (!CheckProbeSuffix(payload, payload_length))
            throw std::runtime_error("malformed PROBE_SUFFIX packets");

        response.probe_suffixes.push_back(payload);
        return;

    case TRANSLATE_AUTH_FILE:
#if TRANSLATION_ENABLE_SESSION
        if (response.HasAuth())
            throw std::runtime_error("duplicate AUTH_FILE packet");

        if (!is_valid_absolute_path(payload, payload_length))
            throw std::runtime_error("malformed AUTH_FILE packet");

        response.auth_file = payload;
        return;
#else
        break;
#endif

    case TRANSLATE_EXPAND_AUTH_FILE:
#if TRANSLATION_ENABLE_SESSION
        if (response.HasAuth())
            throw std::runtime_error("duplicate EXPAND_AUTH_FILE packet");

        if (!is_valid_nonempty_string(payload, payload_length))
            throw std::runtime_error("malformed EXPAND_AUTH_FILE packet");

        if (response.regex == nullptr)
            throw std::runtime_error("misplaced EXPAND_AUTH_FILE packet");

        response.expand_auth_file = payload;
        return;
#else
        break;
#endif

    case TRANSLATE_APPEND_AUTH:
#if TRANSLATION_ENABLE_SESSION
        if (!response.HasAuth() ||
            !response.append_auth.IsNull() ||
            response.expand_append_auth != nullptr)
            throw std::runtime_error("misplaced APPEND_AUTH packet");

        response.append_auth = { payload, payload_length };
        return;
#else
        break;
#endif

    case TRANSLATE_EXPAND_APPEND_AUTH:
#if TRANSLATION_ENABLE_SESSION
        if (response.regex == nullptr ||
            !response.HasAuth() ||
            !response.append_auth.IsNull() ||
            response.expand_append_auth != nullptr)
            throw std::runtime_error("misplaced EXPAND_APPEND_AUTH packet");

        if (!is_valid_nonempty_string(payload, payload_length))
            throw std::runtime_error("malformed EXPAND_APPEND_AUTH packet");

        response.expand_append_auth = payload;
        return;
#else
        break;
#endif

    case TRANSLATE_EXPAND_COOKIE_HOST:
#if TRANSLATION_ENABLE_SESSION
        if (response.regex == nullptr ||
            resource_address == nullptr ||
            !resource_address->IsDefined())
            throw std::runtime_error("misplaced EXPAND_COOKIE_HOST packet");

        if (!is_valid_nonempty_string(payload, payload_length))
            throw std::runtime_error("malformed EXPAND_COOKIE_HOST packet");

        response.expand_cookie_host = payload;
        return;
#else
        break;
#endif

    case TRANSLATE_EXPAND_BIND_MOUNT:
#if TRANSLATION_ENABLE_EXPAND
        HandleBindMount(payload, payload_length, true, false);
        return;
#else
        break;
#endif

    case TRANSLATE_NON_BLOCKING:
#if TRANSLATION_ENABLE_RADDRESS
        if (payload_length > 0)
            throw std::runtime_error("malformed NON_BLOCKING packet");

        if (lhttp_address != nullptr) {
            lhttp_address->blocking = false;
        } else
            throw std::runtime_error("misplaced NON_BLOCKING packet");

        return;
#else
        break;
#endif

    case TRANSLATE_READ_FILE:
        if (response.read_file != nullptr ||
            response.expand_read_file != nullptr)
            throw std::runtime_error("duplicate READ_FILE packet");

        if (!is_valid_absolute_path(payload, payload_length))
            throw std::runtime_error("malformed READ_FILE packet");

        response.read_file = payload;
        return;

    case TRANSLATE_EXPAND_READ_FILE:
#if TRANSLATION_ENABLE_EXPAND
        if (response.read_file != nullptr ||
            response.expand_read_file != nullptr)
            throw std::runtime_error("duplicate EXPAND_READ_FILE packet");

        if (!is_valid_nonempty_string(payload, payload_length))
            throw std::runtime_error("malformed EXPAND_READ_FILE packet");

        response.expand_read_file = payload;
        return;
#else
        break;
#endif

    case TRANSLATE_EXPAND_HEADER:
#if TRANSLATION_ENABLE_HTTP && TRANSLATION_ENABLE_EXPAND
        if (response.regex == nullptr)
            throw std::runtime_error("misplaced EXPAND_HEADER packet");

        parse_header(alloc,
                     response.expand_response_headers,
                     "EXPAND_HEADER", payload, payload_length);
        return;
#else
        break;
#endif

    case TRANSLATE_REGEX_ON_HOST_URI:
#if TRANSLATION_ENABLE_HTTP
        if (response.regex == nullptr &&
            response.inverse_regex == nullptr)
            throw std::runtime_error("REGEX_ON_HOST_URI without REGEX");

        if (response.regex_on_host_uri)
            throw std::runtime_error("duplicate REGEX_ON_HOST_URI");

        if (payload_length > 0)
            throw std::runtime_error("malformed REGEX_ON_HOST_URI packet");

        response.regex_on_host_uri = true;
        return;
#else
        break;
#endif

    case TRANSLATE_SESSION_SITE:
#if TRANSLATION_ENABLE_SESSION
        response.session_site = payload;
        return;
#else
        break;
#endif

    case TRANSLATE_IPC_NAMESPACE:
        if (payload_length != 0)
            throw std::runtime_error("malformed IPC_NAMESPACE packet");

        if (ns_options != nullptr) {
            ns_options->enable_ipc = true;
        } else
            throw std::runtime_error("misplaced IPC_NAMESPACE packet");

        return;

    case TRANSLATE_AUTO_DEFLATE:
        if (payload_length > 0)
            throw std::runtime_error("malformed AUTO_DEFLATE packet");

        if (response.auto_deflate)
            throw std::runtime_error("misplaced AUTO_DEFLATE packet");

        response.auto_deflate = true;
        return;

    case TRANSLATE_EXPAND_HOME:
#if TRANSLATION_ENABLE_EXPAND
        translate_client_expand_home(ns_options,
#if TRANSLATION_ENABLE_JAILCGI
                                     jail,
#endif
                                     payload, payload_length);
        return;
#else
        break;
#endif

    case TRANSLATE_EXPAND_STDERR_PATH:
#if TRANSLATION_ENABLE_EXPAND
        translate_client_expand_stderr_path(child_options,
                                            { _payload, payload_length });
        return;
#else
        break;
#endif

    case TRANSLATE_REGEX_ON_USER_URI:
#if TRANSLATION_ENABLE_HTTP
        if (response.regex == nullptr &&
            response.inverse_regex == nullptr)
            throw std::runtime_error("REGEX_ON_USER_URI without REGEX");

        if (response.regex_on_user_uri)
            throw std::runtime_error("duplicate REGEX_ON_USER_URI");

        if (payload_length > 0)
            throw std::runtime_error("malformed REGEX_ON_USER_URI packet");

        response.regex_on_user_uri = true;
        return;
#else
        break;
#endif

    case TRANSLATE_AUTO_GZIP:
        if (payload_length > 0)
            throw std::runtime_error("malformed AUTO_GZIP packet");

        if (response.auto_gzip)
            throw std::runtime_error("misplaced AUTO_GZIP packet");

        response.auto_gzip = true;
        return;

    case TRANSLATE_INTERNAL_REDIRECT:
#if TRANSLATION_ENABLE_HTTP
        if (!response.internal_redirect.IsNull())
            throw std::runtime_error("duplicate INTERNAL_REDIRECT packet");

        response.internal_redirect = { payload, payload_length };
        return;
#else
        break;
#endif

    case TRANSLATE_REFENCE:
        HandleRefence({payload, payload_length});
        return;

    case TRANSLATE_INVERSE_REGEX_UNESCAPE:
#if TRANSLATION_ENABLE_EXPAND
        if (payload_length > 0)
            throw std::runtime_error("malformed INVERSE_REGEX_UNESCAPE packet");

        if (response.inverse_regex == nullptr)
            throw std::runtime_error("misplaced INVERSE_REGEX_UNESCAPE packet");

        if (response.inverse_regex_unescape)
            throw std::runtime_error("duplicate INVERSE_REGEX_UNESCAPE packet");

        response.inverse_regex_unescape = true;
        return;
#else
        break;
#endif

    case TRANSLATE_BIND_MOUNT_RW:
        HandleBindMount(payload, payload_length, false, true);
        return;

    case TRANSLATE_EXPAND_BIND_MOUNT_RW:
#if TRANSLATION_ENABLE_EXPAND
        HandleBindMount(payload, payload_length, true, true);
        return;
#else
        break;
#endif

    case TRANSLATE_UNTRUSTED_RAW_SITE_SUFFIX:
#if TRANSLATION_ENABLE_SESSION
        if (!is_valid_nonempty_string(payload, payload_length) ||
            payload[payload_length - 1] == '.')
            throw std::runtime_error("malformed UNTRUSTED_RAW_SITE_SUFFIX packet");

        if (response.HasUntrusted())
            throw std::runtime_error("misplaced UNTRUSTED_RAW_SITE_SUFFIX packet");

        response.untrusted_raw_site_suffix = payload;
        return;
#else
        break;
#endif

    case TRANSLATE_MOUNT_TMPFS:
        translate_client_mount_tmpfs(ns_options, payload, payload_length);
        return;

    case TRANSLATE_REVEAL_USER:
#if TRANSLATION_ENABLE_TRANSFORMATION
        if (payload_length > 0)
            throw std::runtime_error("malformed REVEAL_USER packet");

        if (transformation == nullptr ||
            transformation->type != Transformation::Type::FILTER ||
            transformation->u.filter.reveal_user)
            throw std::runtime_error("misplaced REVEAL_USER packet");

        transformation->u.filter.reveal_user = true;
        return;
#else
        break;
#endif

    case TRANSLATE_REALM_FROM_AUTH_BASE:
#if TRANSLATION_ENABLE_SESSION
        if (payload_length > 0)
            throw std::runtime_error("malformed REALM_FROM_AUTH_BASE packet");

        if (response.realm_from_auth_base)
            throw std::runtime_error("duplicate REALM_FROM_AUTH_BASE packet");

        if (response.realm != nullptr || !response.HasAuth())
            throw std::runtime_error("misplaced REALM_FROM_AUTH_BASE packet");

        response.realm_from_auth_base = true;
        return;
#else
        break;
#endif

    case TRANSLATE_NO_NEW_PRIVS:
        if (child_options == nullptr || child_options->no_new_privs)
            throw std::runtime_error("misplaced NO_NEW_PRIVS packet");

        if (payload_length != 0)
            throw std::runtime_error("malformed NO_NEW_PRIVS packet");

        child_options->no_new_privs = true;
        return;

    case TRANSLATE_CGROUP:
        if (child_options == nullptr ||
            child_options->cgroup.name != nullptr)
            throw std::runtime_error("misplaced CGROUP packet");

        if (!valid_view_name(payload))
            throw std::runtime_error("malformed CGROUP packet");

        child_options->cgroup.name = payload;
        return;

    case TRANSLATE_CGROUP_SET:
        HandleCgroupSet({payload, payload_length});
        return;

    case TRANSLATE_EXTERNAL_SESSION_MANAGER:
#if TRANSLATION_ENABLE_SESSION
        if (!is_valid_nonempty_string(payload, payload_length))
            throw std::runtime_error("malformed EXTERNAL_SESSION_MANAGER packet");

        if (response.external_session_manager != nullptr)
            throw std::runtime_error("duplicate EXTERNAL_SESSION_MANAGER packet");

        response.external_session_manager = http_address =
            http_address_parse(alloc, payload);
        if (http_address->protocol != HttpAddress::Protocol::HTTP)
            throw std::runtime_error("malformed EXTERNAL_SESSION_MANAGER packet");

        address_list = &http_address->addresses;
        default_port = http_address->GetDefaultPort();
        return;
#else
        break;
#endif

    case TRANSLATE_EXTERNAL_SESSION_KEEPALIVE: {
#if TRANSLATION_ENABLE_SESSION
        const uint16_t *value = (const uint16_t *)(const void *)payload;
        if (payload_length != sizeof(*value) || *value == 0)
            throw std::runtime_error("malformed EXTERNAL_SESSION_KEEPALIVE packet");

        if (response.external_session_manager == nullptr)
            throw std::runtime_error("misplaced EXTERNAL_SESSION_KEEPALIVE packet");

        if (response.external_session_keepalive != std::chrono::seconds::zero())
            throw std::runtime_error("duplicate EXTERNAL_SESSION_KEEPALIVE packet");

        response.external_session_keepalive = std::chrono::seconds(*value);
        return;
#else
        break;
#endif
    }

    case TRANSLATE_BIND_MOUNT_EXEC:
        HandleBindMount(payload, payload_length, false, false, true);
        return;

    case TRANSLATE_EXPAND_BIND_MOUNT_EXEC:
#if TRANSLATION_ENABLE_EXPAND
        HandleBindMount(payload, payload_length, true, false, true);
        return;
#else
        break;
#endif

    case TRANSLATE_STDERR_NULL:
        if (payload_length > 0)
            throw std::runtime_error("malformed STDERR_NULL packet");

        if (child_options == nullptr || child_options->stderr_path != nullptr)
            throw std::runtime_error("misplaced STDERR_NULL packet");

        if (child_options->stderr_null)
            throw std::runtime_error("duplicate STDERR_NULL packet");

        child_options->stderr_null = true;
        return;

    case TRANSLATE_EXECUTE:
#if TRANSLATION_ENABLE_EXECUTE
        if (!is_valid_absolute_path(payload, payload_length))
            throw std::runtime_error("malformed EXECUTE packet");

        if (response.execute != nullptr)
            throw std::runtime_error("duplicate EXECUTE packet");

        response.execute = payload;
        return;
#else
        break;
#endif
    }

    throw FormatRuntimeError("unknown translation packet: %u", command);
}

inline TranslateParser::Result
TranslateParser::HandlePacket(enum beng_translation_command command,
                              const void *const payload, size_t payload_length)
{
    assert(payload != nullptr);

    if (command == TRANSLATE_BEGIN) {
        if (begun)
            throw std::runtime_error("double BEGIN from translation server");
    } else {
        if (!begun)
            throw std::runtime_error("no BEGIN from translation server");
    }

    switch (command) {
    case TRANSLATE_END:
        translate_response_finish(&response);

#if TRANSLATION_ENABLE_WIDGET
        FinishView();
#endif
        return Result::DONE;

    case TRANSLATE_BEGIN:
        begun = true;
        response.Clear();
        previous_command = command;
#if TRANSLATION_ENABLE_RADDRESS
        resource_address = &response.address;
#endif
#if TRANSLATION_ENABLE_JAILCGI
        jail = nullptr;
#endif
#if TRANSLATION_ENABLE_EXECUTE
        args_builder = response.args;
        SetChildOptions(response.child_options);
#else
        child_options = nullptr;
        ns_options = nullptr;
        mount_list = nullptr;
#endif
#if TRANSLATION_ENABLE_RADDRESS
        file_address = nullptr;
        http_address = nullptr;
        cgi_address = nullptr;
        nfs_address = nullptr;
        lhttp_address = nullptr;
        address_list = nullptr;
#endif

#if TRANSLATION_ENABLE_WIDGET
        response.views = alloc.New<WidgetView>();
        response.views->Init(nullptr);
        view = nullptr;
        widget_view_tail = &response.views->next;
#endif

#if TRANSLATION_ENABLE_TRANSFORMATION
        transformation = nullptr;
        transformation_tail = &response.views->transformation;
#endif

        if (payload_length >= sizeof(uint8_t))
            response.protocol_version = *(const uint8_t *)payload;

        return Result::MORE;

    default:
        HandleRegularPacket(command, payload, payload_length);
        return Result::MORE;
    }
}

TranslateParser::Result
TranslateParser::Process()
{
    if (!reader.IsComplete())
        /* need more data */
        return Result::MORE;

    return HandlePacket(reader.GetCommand(),
                        reader.GetPayload(), reader.GetLength());
}
