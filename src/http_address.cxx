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
#include "util/StringView.hxx"

#include <socket/address.h>

#include <string.h>

static bool
uri_scheme_has_host(enum uri_scheme scheme)
{
    return scheme != URI_SCHEME_UNIX;
}

void
HttpAddress::Init(enum uri_scheme _scheme, bool _ssl,
                  const char *_host_and_port, const char *_path)
{
    scheme = _scheme;
    ssl = _ssl;
    host_and_port = _host_and_port;
    path = _path;
    expand_path = nullptr;
    addresses.Init();
}

static HttpAddress *
http_address_new(struct pool &pool, enum uri_scheme scheme, bool ssl,
                 const char *host_and_port, const char *path)
{
    assert(uri_scheme_has_host(scheme) == (host_and_port != nullptr));
    assert(path != nullptr);

    auto uwa = NewFromPool<HttpAddress>(pool);
    uwa->Init(scheme, ssl, host_and_port, path);
    return uwa;
}

/**
 * Utility function used by http_address_parse().
 */
static HttpAddress *
http_address_parse2(struct pool *pool, enum uri_scheme scheme, bool ssl,
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

    return http_address_new(*pool, scheme, ssl, host_and_port, path);
}

HttpAddress *
http_address_parse(struct pool *pool, const char *uri, GError **error_r)
{
    if (memcmp(uri, "http://", 7) == 0)
        return http_address_parse2(pool, URI_SCHEME_HTTP, false, uri + 7,
                                   error_r);
    else if (memcmp(uri, "https://", 8) == 0)
        return http_address_parse2(pool, URI_SCHEME_HTTP, true, uri + 8,
                                   error_r);
    else if (memcmp(uri, "ajp://", 6) == 0)
        return http_address_parse2(pool, URI_SCHEME_AJP, false, uri + 6,
                                   error_r);
    else if (memcmp(uri, "unix:/", 6) == 0)
        return http_address_new(*pool, URI_SCHEME_UNIX, false,
                                nullptr, uri + 5);

    g_set_error(error_r, http_address_quark(), 0,
                "unrecognized URI");
    return nullptr;
}

HttpAddress *
http_address_with_path(struct pool &pool, const HttpAddress *uwa,
                       const char *path)
{
    HttpAddress *p =
        http_address_new(pool, uwa->scheme, uwa->ssl,
                         uwa->host_and_port, path);
    p->expand_path = p_strdup_checked(&pool, uwa->expand_path);
    p->addresses.CopyFrom(&pool, uwa->addresses);
    return p;
}

HttpAddress *
http_address_dup(struct pool &pool, const HttpAddress *uwa)
{
    assert(uwa != nullptr);

    HttpAddress *p =
        http_address_new(pool, uwa->scheme, uwa->ssl,
                         p_strdup(&pool, uwa->host_and_port),
                         p_strdup(&pool, uwa->path));

    p->expand_path = p_strdup_checked(&pool, uwa->expand_path);
    p->addresses.CopyFrom(&pool, uwa->addresses);

    return p;
}

HttpAddress *
http_address_dup_with_path(struct pool &pool,
                           const HttpAddress *uwa,
                           const char *path)
{
    HttpAddress *p =
        http_address_new(pool, uwa->scheme, uwa->ssl,
                         p_strdup(&pool, uwa->host_and_port),
                         path);
    p->expand_path = p_strdup_checked(&pool, uwa->expand_path);
    p->addresses.CopyFrom(&pool, uwa->addresses);
    return p;
}

gcc_const
static const char *
uri_scheme_prefix(enum uri_scheme p)
{
    switch (p) {
    case URI_SCHEME_UNIX:
        return "unix:";

    case URI_SCHEME_HTTP:
        return "http://";

    case URI_SCHEME_AJP:
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

    return p_strcat(pool, uri_scheme_prefix(scheme),
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
                        const char *args, size_t args_length,
                        const char *path_info, size_t path_info_length) const
{
    return http_address_with_path(pool, this,
                                  uri_insert_args(&pool, path,
                                                  args, args_length,
                                                  path_info, path_info_length));
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
HttpAddress::Apply(struct pool *pool, const char *relative,
                   size_t relative_length) const
{
    if (relative_length == 0)
        return this;

    if (uri_has_protocol({relative, relative_length})) {
        HttpAddress *other =
            http_address_parse(pool, p_strndup(pool, relative, relative_length),
                               nullptr);
        if (other == nullptr || other->scheme != scheme)
            return nullptr;

        if (uri_scheme_has_host(other->scheme) &&
            strcmp(other->host_and_port, host_and_port) != 0)
            /* if it points to a different host, we cannot apply the
               address list, and so this function must fail */
            return nullptr;

        other->addresses.CopyFrom(pool, addresses);
        return other;
    }

    const char *p = uri_absolute(pool, path,
                                 relative, relative_length);
    assert(p != nullptr);

    return http_address_with_path(*pool, this, p);
}

StringView
http_address_relative(const HttpAddress *base,
                      const HttpAddress *uwa)
{
    if (base->scheme != uwa->scheme)
        return nullptr;

    if (uri_scheme_has_host(base->scheme) &&
        strcmp(base->host_and_port, uwa->host_and_port) != 0)
        return nullptr;

    return uri_relative(base->path, uwa->path);
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
