/*
 * Store a URI along with a list of socket addresses.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "http_address.hxx"
#include "uri/uri_base.hxx"
#include "uri/uri_relative.hxx"
#include "uri/uri_verify.hxx"
#include "uri/uri_extract.hxx"
#include "puri_edit.hxx"
#include "puri_relative.hxx"
#include "pool.hxx"
#include "pexpand.hxx"
#include "translate_quark.hxx"
#include "util/StringView.hxx"

#include <socket/address.h>

#include <glib.h>

#include <string.h>

gcc_const
static inline GQuark
http_address_quark(void)
{
    return g_quark_from_static_string("http_address");
}

HttpAddress::HttpAddress(Protocol _protocol, bool _ssl,
                         const char *_host_and_port, const char *_path)
    :protocol(_protocol), ssl(_ssl),
     host_and_port(_host_and_port),
     path(_path),
     expand_path(nullptr)
{
}

HttpAddress::HttpAddress(ShallowCopy shallow_copy,
                         Protocol _protocol, bool _ssl,
                         const char *_host_and_port, const char *_path,
                         const AddressList &_addresses)
    :protocol(_protocol), ssl(_ssl),
     host_and_port(_host_and_port),
     path(_path),
     expand_path(nullptr),
     addresses(shallow_copy, _addresses)
{
}

HttpAddress::HttpAddress(struct pool &pool, const HttpAddress &src)
    :protocol(src.protocol), ssl(src.ssl),
     host_and_port(p_strdup_checked(&pool, src.host_and_port)),
     path(p_strdup(&pool, src.path)),
     expand_path(p_strdup_checked(&pool, src.expand_path)),
     addresses(pool, src.addresses)
{
}

HttpAddress::HttpAddress(struct pool &pool, const HttpAddress &src,
                         const char *_path)
    :protocol(src.protocol), ssl(src.ssl),
     host_and_port(p_strdup_checked(&pool, src.host_and_port)),
     path(p_strdup(&pool, _path)),
     expand_path(nullptr),
     addresses(pool, src.addresses)
{
}

static HttpAddress *
http_address_new(struct pool &pool, HttpAddress::Protocol protocol, bool ssl,
                 const char *host_and_port, const char *path)
{
    assert(path != nullptr);

    return NewFromPool<HttpAddress>(pool, protocol, ssl, host_and_port, path);
}

/**
 * Utility function used by http_address_parse().
 */
static HttpAddress *
http_address_parse2(struct pool *pool, HttpAddress::Protocol protocol, bool ssl,
                    const char *uri, GError **error_r)
{
    assert(pool != nullptr);
    assert(uri != nullptr);

    const char *path = strchr(uri, '/');
    const char *host_and_port;
    if (path != nullptr) {
        if (path == uri || !uri_path_verify_quick(path)) {
            g_set_error(error_r, http_address_quark(), 0,
                        "malformed HTTP URI");
            return nullptr;
        }

        host_and_port = p_strndup(pool, uri, path - uri);
        path = p_strdup(pool, path);
    } else {
        host_and_port = p_strdup(pool, uri);
        path = "/";
    }

    return http_address_new(*pool, protocol, ssl, host_and_port, path);
}

HttpAddress *
http_address_parse(struct pool *pool, const char *uri, GError **error_r)
{
    if (memcmp(uri, "http://", 7) == 0)
        return http_address_parse2(pool, HttpAddress::Protocol::HTTP,
                                   false, uri + 7,
                                   error_r);
    else if (memcmp(uri, "https://", 8) == 0)
        return http_address_parse2(pool, HttpAddress::Protocol::HTTP,
                                   true, uri + 8,
                                   error_r);
    else if (memcmp(uri, "ajp://", 6) == 0)
        return http_address_parse2(pool, HttpAddress::Protocol::AJP,
                                   false, uri + 6,
                                   error_r);
    else if (memcmp(uri, "unix:/", 6) == 0)
        return http_address_new(*pool, HttpAddress::Protocol::HTTP,
                                false, nullptr, uri + 5);

    g_set_error(error_r, http_address_quark(), 0,
                "unrecognized URI");
    return nullptr;
}

HttpAddress *
http_address_with_path(struct pool &pool, const HttpAddress *uwa,
                       const char *path)
{
    auto *p = NewFromPool<HttpAddress>(pool, ShallowCopy(), *uwa);
    p->path = path;
    return p;
}

HttpAddress *
http_address_dup(struct pool &pool, const HttpAddress *uwa)
{
    assert(uwa != nullptr);

    return NewFromPool<HttpAddress>(pool, pool, *uwa);
}

HttpAddress *
http_address_dup_with_path(struct pool &pool,
                           const HttpAddress *uwa,
                           const char *path)
{
    assert(uwa != nullptr);

    return NewFromPool<HttpAddress>(pool, pool, *uwa, path);
}

bool
HttpAddress::Check(GError **error_r) const
{
    if (addresses.IsEmpty()) {
        g_set_error_literal(error_r, translate_quark(), 0,
                            protocol == Protocol::AJP
                            ? "no ADDRESS for AJP address"
                            :"no ADDRESS for HTTP address");
        return false;
    }

    return true;
}

gcc_const
static const char *
uri_protocol_prefix(HttpAddress::Protocol p, bool has_host)
{
    switch (p) {
    case HttpAddress::Protocol::HTTP:
        return has_host ? "http://" : "unix:";

    case HttpAddress::Protocol::AJP:
        return "ajp://";
    }

    assert(false);
    return nullptr;
}

char *
HttpAddress::GetAbsoluteURI(struct pool *pool,
                            const char *override_path) const
{
    assert(pool != nullptr);
    assert(host_and_port != nullptr);
    assert(override_path != nullptr);
    assert(*override_path == '/');

    return p_strcat(pool, uri_protocol_prefix(protocol, host_and_port != nullptr),
                    host_and_port == nullptr ? "" : host_and_port,
                    override_path, nullptr);
}

char *
HttpAddress::GetAbsoluteURI(struct pool *pool) const
{
    assert(pool != nullptr);

    return GetAbsoluteURI(pool, path);
}

bool
HttpAddress::HasQueryString() const
{
        return strchr(path, '?') != nullptr;
}

HttpAddress *
HttpAddress::InsertQueryString(struct pool &pool,
                               const char *query_string) const
{
    return http_address_with_path(pool, this,
                                  uri_insert_query_string(&pool, path,
                                                          query_string));
}

HttpAddress *
HttpAddress::InsertArgs(struct pool &pool,
                        StringView args, StringView path_info) const
{
    return http_address_with_path(pool, this,
                                  uri_insert_args(&pool, path,
                                                  args, path_info));
}

bool
HttpAddress::IsValidBase() const
{
    return IsExpandable() || is_base(path);
}

HttpAddress *
HttpAddress::SaveBase(struct pool *pool, const char *suffix) const
{
    assert(pool != nullptr);
    assert(suffix != nullptr);

    size_t length = base_string(path, suffix);
    if (length == (size_t)-1)
        return nullptr;

    return http_address_dup_with_path(*pool, this,
                                      p_strndup(pool, path, length));
}

HttpAddress *
HttpAddress::LoadBase(struct pool *pool, const char *suffix) const
{
    assert(pool != nullptr);
    assert(suffix != nullptr);
    assert(path != nullptr);
    assert(*path != 0);
    assert(expand_path != nullptr ||
           path[strlen(path) - 1] == '/');

    return http_address_dup_with_path(*pool, this,
                                      p_strcat(pool, path, suffix, nullptr));
}

const HttpAddress *
HttpAddress::Apply(struct pool *pool, StringView relative) const
{
    if (relative.IsEmpty())
        return this;

    if (uri_has_protocol(relative)) {
        HttpAddress *other =
            http_address_parse(pool, p_strdup(*pool, relative),
                               nullptr);
        if (other == nullptr || other->protocol != protocol)
            return nullptr;

        const char *my_host = host_and_port != nullptr ? host_and_port : "";
        const char *other_host = other->host_and_port != nullptr
            ? other->host_and_port
            : "";

        if (strcmp(my_host, other_host) != 0)
            /* if it points to a different host, we cannot apply the
               address list, and so this function must fail */
            return nullptr;

        other->addresses = AddressList(ShallowCopy(), addresses);
        return other;
    }

    const char *p = uri_absolute(pool, path, relative);
    assert(p != nullptr);

    return http_address_with_path(*pool, this, p);
}

StringView
HttpAddress::RelativeTo(const HttpAddress &base) const
{
    if (base.protocol != protocol)
        return nullptr;

    const char *my_host = host_and_port != nullptr ? host_and_port : "";
    const char *base_host = base.host_and_port != nullptr
        ? base.host_and_port
        : "";

    if (strcmp(my_host, base_host) != 0)
        return nullptr;

    return uri_relative(base.path, path);
}

bool
HttpAddress::Expand(struct pool *pool, const MatchInfo &match_info,
                    Error &error_r)
{
    assert(pool != nullptr);

    if (expand_path != nullptr) {
        path = expand_string(pool, expand_path, match_info, error_r);
        if (path == nullptr)
            return false;
    }

    return true;
}
