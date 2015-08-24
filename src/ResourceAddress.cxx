/*
 * Address of a resource, which might be a local file, a CGI script or
 * a HTTP server.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "ResourceAddress.hxx"
#include "file_address.hxx"
#include "lhttp_address.hxx"
#include "http_address.hxx"
#include "cgi_address.hxx"
#include "nfs_address.hxx"
#include "uri_relative.hxx"
#include "uri_edit.hxx"
#include "uri_extract.hxx"
#include "uri_verify.hxx"
#include "uri_base.hxx"
#include "strref.h"
#include "pool.hxx"
#include "http_quark.h"

#include <http/status.h>

void
ResourceAddress::CopyFrom(struct pool &pool, const ResourceAddress &src)
{
    type = src.type;

    switch (src.type) {
    case RESOURCE_ADDRESS_NONE:
        break;

    case RESOURCE_ADDRESS_LOCAL:
        assert(src.u.file != nullptr);
        u.file = file_address_dup(pool, src.u.file);
        break;

    case RESOURCE_ADDRESS_HTTP:
    case RESOURCE_ADDRESS_AJP:
        assert(src.u.http != nullptr);
        u.http = http_address_dup(pool, src.u.http);
        break;

    case RESOURCE_ADDRESS_LHTTP:
        assert(src.u.lhttp != nullptr);
        u.lhttp = lhttp_address_dup(pool, src.u.lhttp);
        break;

    case RESOURCE_ADDRESS_PIPE:
    case RESOURCE_ADDRESS_CGI:
    case RESOURCE_ADDRESS_FASTCGI:
    case RESOURCE_ADDRESS_WAS:
        u.cgi = cgi_address_dup(pool, src.u.cgi,
                                      src.type == RESOURCE_ADDRESS_FASTCGI);
        break;

    case RESOURCE_ADDRESS_NFS:
        u.nfs = nfs_address_dup(pool, src.u.nfs);
        break;
    }
}

ResourceAddress *
ResourceAddress::Dup(struct pool &pool) const
{
    auto dest = NewFromPool<ResourceAddress>(pool);
    dest->CopyFrom(pool, *this);
    return dest;
}

ResourceAddress *
ResourceAddress::DupWithPath(struct pool &pool, const char *path) const
{
    auto dest = NewFromPool<ResourceAddress>(pool);
    dest->type = type;

    switch (type) {
    case RESOURCE_ADDRESS_NONE:
    case RESOURCE_ADDRESS_LOCAL:
    case RESOURCE_ADDRESS_PIPE:
    case RESOURCE_ADDRESS_NFS:
    case RESOURCE_ADDRESS_CGI:
    case RESOURCE_ADDRESS_FASTCGI:
    case RESOURCE_ADDRESS_WAS:
        assert(false);
        gcc_unreachable();

    case RESOURCE_ADDRESS_HTTP:
    case RESOURCE_ADDRESS_AJP:
        dest->u.http = http_address_dup_with_path(pool, u.http, path);
        break;

    case RESOURCE_ADDRESS_LHTTP:
        dest->u.lhttp = lhttp_address_dup_with_uri(pool, u.lhttp, path);
        break;
    }

    return dest;
}

const ResourceAddress *
ResourceAddress::DupWithQueryStringFrom(struct pool &pool, const char *uri) const
{
    const char *query_string;
    ResourceAddress *dest;

    switch (type) {
        struct cgi_address *cgi;

    case RESOURCE_ADDRESS_NONE:
    case RESOURCE_ADDRESS_LOCAL:
    case RESOURCE_ADDRESS_PIPE:
    case RESOURCE_ADDRESS_NFS:
        /* no query string support */
        return this;

    case RESOURCE_ADDRESS_HTTP:
    case RESOURCE_ADDRESS_AJP:
        assert(u.http != nullptr);

        query_string = uri_query_string(uri);
        if (query_string == nullptr)
            /* no query string in URI */
            return this;

        dest = NewFromPool<ResourceAddress>(pool);
        dest->type = type;
        dest->u.http = u.http->InsertQueryString(pool, query_string);
        return dest;

    case RESOURCE_ADDRESS_LHTTP:
        assert(u.lhttp != nullptr);

        query_string = uri_query_string(uri);
        if (query_string == nullptr)
            /* no query string in URI */
            return this;

        dest = NewFromPool<ResourceAddress>(pool);
        dest->type = type;
        dest->u.lhttp = u.lhttp->InsertQueryString(pool, query_string);
        return dest;

    case RESOURCE_ADDRESS_CGI:
    case RESOURCE_ADDRESS_FASTCGI:
    case RESOURCE_ADDRESS_WAS:
        assert(u.cgi->path != nullptr);

        query_string = uri_query_string(uri);
        if (query_string == nullptr)
            /* no query string in URI */
            return this;

        dest = Dup(pool);
        cgi = dest->GetCgi();

        if (cgi->query_string != nullptr)
            cgi->query_string = p_strcat(&pool, query_string, "&",
                                         cgi->query_string, nullptr);
        else
            cgi->query_string = p_strdup(&pool, query_string);
        return dest;
    }

    assert(false);
    gcc_unreachable();
}

const ResourceAddress *
ResourceAddress::DupWithArgs(struct pool &pool,
                             const char *args, size_t args_length,
                             const char *path, size_t path_length) const
{
    ResourceAddress *dest;

    switch (type) {
        struct cgi_address *cgi;

    case RESOURCE_ADDRESS_NONE:
    case RESOURCE_ADDRESS_LOCAL:
    case RESOURCE_ADDRESS_PIPE:
    case RESOURCE_ADDRESS_NFS:
        /* no arguments support */
        return this;

    case RESOURCE_ADDRESS_HTTP:
    case RESOURCE_ADDRESS_AJP:
        assert(u.http != nullptr);

        dest = NewFromPool<ResourceAddress>(pool);
        dest->type = type;
        dest->u.http = u.http->InsertArgs(pool,
                                               args, args_length,
                                               path, path_length);
        return dest;

    case RESOURCE_ADDRESS_LHTTP:
        assert(u.lhttp != nullptr);

        dest = NewFromPool<ResourceAddress>(pool);
        dest->type = type;
        dest->u.lhttp = u.lhttp->InsertArgs(pool,
                                                 args, args_length,
                                                 path, path_length);
        return dest;

    case RESOURCE_ADDRESS_CGI:
    case RESOURCE_ADDRESS_FASTCGI:
    case RESOURCE_ADDRESS_WAS:
        assert(u.cgi->path != nullptr);

        if (u.cgi->uri == nullptr && u.cgi->path_info == nullptr)
            return this;

        dest = Dup(pool);
        cgi = dest->GetCgi();

        if (cgi->uri != nullptr)
            cgi->uri = uri_insert_args(&pool, cgi->uri,
                                       args, args_length,
                                       path, path_length);

        if (cgi->path_info != nullptr)
            cgi->path_info =
                p_strncat(&pool,
                          cgi->path_info, strlen(cgi->path_info),
                          ";", (size_t)1, args, args_length, path, path_length,
                          nullptr);

        return dest;
    }

    assert(false);
    gcc_unreachable();
}

char *
ResourceAddress::AutoBase(struct pool &pool, const char *uri) const
{
    assert(uri != nullptr);

    switch (type) {
    case RESOURCE_ADDRESS_NONE:
    case RESOURCE_ADDRESS_LOCAL:
    case RESOURCE_ADDRESS_PIPE:
    case RESOURCE_ADDRESS_HTTP:
    case RESOURCE_ADDRESS_LHTTP:
    case RESOURCE_ADDRESS_AJP:
    case RESOURCE_ADDRESS_NFS:
        return nullptr;

    case RESOURCE_ADDRESS_CGI:
    case RESOURCE_ADDRESS_FASTCGI:
    case RESOURCE_ADDRESS_WAS:
        return u.cgi->AutoBase(&pool, uri);
    }

    assert(false);
    gcc_unreachable();
}

ResourceAddress *
ResourceAddress::SaveBase(struct pool &pool, ResourceAddress &dest,
                          const char *suffix) const
{
    assert(this != &dest);

    switch (type) {
    case RESOURCE_ADDRESS_NONE:
    case RESOURCE_ADDRESS_PIPE:
        return nullptr;

    case RESOURCE_ADDRESS_CGI:
    case RESOURCE_ADDRESS_FASTCGI:
    case RESOURCE_ADDRESS_WAS:
        dest.u.cgi = u.cgi->SaveBase(&pool, suffix,
                                     type == RESOURCE_ADDRESS_FASTCGI);
        if (dest.u.cgi == nullptr)
            return nullptr;

        dest.type = type;
        return &dest;

    case RESOURCE_ADDRESS_LOCAL:
        dest.u.file = u.file->SaveBase(&pool, suffix);
        if (dest.u.file == nullptr)
            return nullptr;

        dest.type = type;
        return &dest;

    case RESOURCE_ADDRESS_HTTP:
    case RESOURCE_ADDRESS_AJP:
        dest.u.http = u.http->SaveBase(&pool, suffix);
        if (dest.u.http == nullptr)
            return nullptr;

        dest.type = type;
        return &dest;

    case RESOURCE_ADDRESS_LHTTP:
        dest.u.lhttp = u.lhttp->SaveBase(&pool, suffix);
        if (dest.u.lhttp == nullptr)
            return nullptr;

        dest.type = type;
        return &dest;

    case RESOURCE_ADDRESS_NFS:
        dest.u.nfs = u.nfs->SaveBase(&pool, suffix);
        if (dest.u.nfs == nullptr)
            return nullptr;

        dest.type = type;
        return &dest;
    }

    assert(false);
    gcc_unreachable();
}

bool
ResourceAddress::CacheStore(struct pool *pool,
                            const ResourceAddress *src,
                            const char *uri, const char *base,
                            bool easy_base, bool expandable)
{
    const char *tail = base_tail(uri, base);
    if (tail != nullptr) {
        /* we received a valid BASE packet - store only the base
           URI */

        if (easy_base || expandable) {
            /* when the response is expandable, skip appending the
               tail URI, don't call resource_address_save_base() */
            CopyFrom(*pool, *src);
            return true;
        }

        if (src->type == RESOURCE_ADDRESS_NONE) {
            /* _save_base() will fail on a "NONE" address, but in this
               case, the operation is useful and is allowed as a
               special case */
            type = RESOURCE_ADDRESS_NONE;
            return true;
        }

        if (src->SaveBase(*pool, *this, tail) != nullptr)
            return true;
    }

    CopyFrom(*pool, *src);
    return false;
}

ResourceAddress *
ResourceAddress::LoadBase(struct pool &pool, ResourceAddress &dest,
                          const char *suffix) const
{
    switch (type) {
    case RESOURCE_ADDRESS_NONE:
    case RESOURCE_ADDRESS_PIPE:
        assert(false);
        gcc_unreachable();

    case RESOURCE_ADDRESS_CGI:
    case RESOURCE_ADDRESS_FASTCGI:
    case RESOURCE_ADDRESS_WAS:
        dest.type = type;
        dest.u.cgi = u.cgi->LoadBase(&pool, suffix,
                                     type == RESOURCE_ADDRESS_FASTCGI);
        return &dest;

    case RESOURCE_ADDRESS_LOCAL:
        dest.type = type;
        dest.u.file = u.file->LoadBase(&pool, suffix);
        return &dest;

    case RESOURCE_ADDRESS_HTTP:
    case RESOURCE_ADDRESS_AJP:
        dest.u.http = u.http->LoadBase(&pool, suffix);
        if (dest.u.http == nullptr)
            return nullptr;

        dest.type = type;
        return &dest;

    case RESOURCE_ADDRESS_LHTTP:
        dest.u.lhttp = u.lhttp->LoadBase(&pool, suffix);
        if (dest.u.lhttp == nullptr)
            return nullptr;

        dest.type = type;
        return &dest;

    case RESOURCE_ADDRESS_NFS:
        dest.u.nfs = u.nfs->LoadBase(&pool, suffix);
        assert(dest.u.nfs != nullptr);
        dest.type = type;
        return &dest;
    }

    assert(false);
    gcc_unreachable();
}

bool
ResourceAddress::CacheLoad(struct pool *pool,
                           const ResourceAddress &src,
                           const char *uri, const char *base,
                           bool unsafe_base, bool expandable,
                           GError **error_r)
{
    if (base != nullptr && !expandable) {
        const char *tail = require_base_tail(uri, base);

        if (!unsafe_base && !uri_path_verify_paranoid(tail - 1)) {
            g_set_error(error_r, http_response_quark(),
                        HTTP_STATUS_BAD_REQUEST, "Malformed URI");
            return false;
        }

        if (src.type == RESOURCE_ADDRESS_NONE) {
            /* see code comment in tcache_store_address() */
            type = RESOURCE_ADDRESS_NONE;
            return true;
        }

        if (src.LoadBase(*pool, *this, tail) != nullptr)
            return true;
    }

    CopyFrom(*pool, src);
    return true;
}

const ResourceAddress *
ResourceAddress::Apply(struct pool &pool,
                       const char *relative, size_t relative_length,
                       ResourceAddress &buffer) const
{
    const struct http_address *uwa;
    const struct cgi_address *cgi;
    const LhttpAddress *lhttp;

    assert(relative != nullptr || relative_length == 0);

    switch (type) {
    case RESOURCE_ADDRESS_NONE:
        return nullptr;

    case RESOURCE_ADDRESS_LOCAL:
    case RESOURCE_ADDRESS_PIPE:
    case RESOURCE_ADDRESS_NFS:
        return this;

    case RESOURCE_ADDRESS_HTTP:
    case RESOURCE_ADDRESS_AJP:
        uwa = u.http->Apply(&pool, relative, relative_length);
        if (uwa == nullptr)
            return nullptr;

        if (uwa == u.http)
            return this;

        buffer.type = type;
        buffer.u.http = uwa;
        return &buffer;

    case RESOURCE_ADDRESS_LHTTP:
        lhttp = u.lhttp->Apply(&pool, relative, relative_length);
        if (lhttp == nullptr)
            return nullptr;

        if (lhttp == u.lhttp)
            return this;

        buffer.type = type;
        buffer.u.lhttp = lhttp;
        return &buffer;

    case RESOURCE_ADDRESS_CGI:
    case RESOURCE_ADDRESS_FASTCGI:
    case RESOURCE_ADDRESS_WAS:
        cgi = u.cgi->Apply(&pool, relative, relative_length,
                           type == RESOURCE_ADDRESS_FASTCGI);
        if (cgi == nullptr)
            return nullptr;

        if (cgi == u.cgi)
            return this;

        buffer.type = type;
        buffer.u.cgi = cgi;
        return &buffer;
    }

    assert(false);
    gcc_unreachable();
}

const struct strref *
ResourceAddress::RelativeTo(const ResourceAddress &base,
                            struct strref &buffer) const
{
    assert(base.type == type);

    switch (type) {
        struct strref base_uri;

    case RESOURCE_ADDRESS_NONE:
    case RESOURCE_ADDRESS_LOCAL:
    case RESOURCE_ADDRESS_PIPE:
    case RESOURCE_ADDRESS_NFS:
        return nullptr;

    case RESOURCE_ADDRESS_HTTP:
    case RESOURCE_ADDRESS_AJP:
        return http_address_relative(base.u.http, u.http, &buffer);

    case RESOURCE_ADDRESS_LHTTP:
        return lhttp_address_relative(base.u.lhttp, u.lhttp, &buffer);

    case RESOURCE_ADDRESS_CGI:
    case RESOURCE_ADDRESS_FASTCGI:
    case RESOURCE_ADDRESS_WAS:
        strref_set_c(&base_uri, base.u.cgi->path_info != nullptr
                     ? base.u.cgi->path_info : "");
        strref_set_c(&buffer, u.cgi->path_info != nullptr
                     ? u.cgi->path_info : "");
        return uri_relative(&base_uri, &buffer);
    }

    assert(false);
    gcc_unreachable();
}

const char *
ResourceAddress::GetId(struct pool &pool) const
{
    switch (type) {
    case RESOURCE_ADDRESS_NONE:
        return "";

    case RESOURCE_ADDRESS_LOCAL:
        return p_strdup(&pool, u.file->path);

    case RESOURCE_ADDRESS_HTTP:
    case RESOURCE_ADDRESS_AJP:
        return u.http->GetAbsoluteURI(&pool);

    case RESOURCE_ADDRESS_LHTTP:
        return u.lhttp->GetId(&pool);

    case RESOURCE_ADDRESS_PIPE:
    case RESOURCE_ADDRESS_CGI:
    case RESOURCE_ADDRESS_FASTCGI:
    case RESOURCE_ADDRESS_WAS:
        return u.cgi->GetId(&pool);

    case RESOURCE_ADDRESS_NFS:
        return u.nfs->GetId(&pool);
    }

    assert(false);
    gcc_unreachable();
}

const char *
ResourceAddress::GetHostAndPort() const
{
    switch (type) {
    case RESOURCE_ADDRESS_NONE:
    case RESOURCE_ADDRESS_LOCAL:
    case RESOURCE_ADDRESS_PIPE:
    case RESOURCE_ADDRESS_CGI:
    case RESOURCE_ADDRESS_FASTCGI:
    case RESOURCE_ADDRESS_WAS:
    case RESOURCE_ADDRESS_NFS:
        return nullptr;

    case RESOURCE_ADDRESS_HTTP:
    case RESOURCE_ADDRESS_AJP:
        return u.http->host_and_port;

    case RESOURCE_ADDRESS_LHTTP:
        return u.lhttp->host_and_port;
    }

    assert(false);
    gcc_unreachable();
}

const char *
ResourceAddress::GetUriPath() const
{
    switch (type) {
    case RESOURCE_ADDRESS_NONE:
    case RESOURCE_ADDRESS_LOCAL:
    case RESOURCE_ADDRESS_PIPE:
    case RESOURCE_ADDRESS_NFS:
        return nullptr;

    case RESOURCE_ADDRESS_HTTP:
    case RESOURCE_ADDRESS_AJP:
        return u.http->path;

    case RESOURCE_ADDRESS_LHTTP:
        return u.lhttp->uri;

    case RESOURCE_ADDRESS_CGI:
    case RESOURCE_ADDRESS_FASTCGI:
    case RESOURCE_ADDRESS_WAS:
        if (u.cgi->uri != nullptr)
            return u.cgi->uri;

        return u.cgi->script_name;
    }

    assert(false);
    gcc_unreachable();
}

bool
ResourceAddress::Check(GError **error_r) const
{
    switch (type) {
    case RESOURCE_ADDRESS_NONE:
    case RESOURCE_ADDRESS_HTTP:
        return true;

    case RESOURCE_ADDRESS_LOCAL:
        return u.file->Check(error_r);

    case RESOURCE_ADDRESS_LHTTP:
        return u.lhttp->Check(error_r);

    case RESOURCE_ADDRESS_PIPE:
    case RESOURCE_ADDRESS_CGI:
    case RESOURCE_ADDRESS_FASTCGI:
    case RESOURCE_ADDRESS_WAS:
        return u.cgi->Check(error_r);

    case RESOURCE_ADDRESS_AJP:
        return true;

    case RESOURCE_ADDRESS_NFS:
        return u.nfs->Check(error_r);
    }

    return true;
}

bool
ResourceAddress::IsValidBase() const
{
    switch (type) {
    case RESOURCE_ADDRESS_NONE:
        return true;

    case RESOURCE_ADDRESS_LOCAL:
        return u.file->IsValidBase();

    case RESOURCE_ADDRESS_HTTP:
    case RESOURCE_ADDRESS_AJP:
        return u.http->IsValidBase();

    case RESOURCE_ADDRESS_LHTTP:
        return u.lhttp->IsValidBase();

    case RESOURCE_ADDRESS_PIPE:
    case RESOURCE_ADDRESS_CGI:
    case RESOURCE_ADDRESS_FASTCGI:
    case RESOURCE_ADDRESS_WAS:
        return u.cgi->IsValidBase();

    case RESOURCE_ADDRESS_NFS:
        return u.nfs->IsValidBase();
    }

    assert(false);
    gcc_unreachable();
}

bool
ResourceAddress::HasQueryString() const
{
    switch (type) {
    case RESOURCE_ADDRESS_NONE:
        return false;

    case RESOURCE_ADDRESS_LOCAL:
        return u.file->HasQueryString();

    case RESOURCE_ADDRESS_HTTP:
    case RESOURCE_ADDRESS_AJP:
        return u.http->HasQueryString();

    case RESOURCE_ADDRESS_LHTTP:
        return u.lhttp->HasQueryString();

    case RESOURCE_ADDRESS_PIPE:
    case RESOURCE_ADDRESS_CGI:
    case RESOURCE_ADDRESS_FASTCGI:
    case RESOURCE_ADDRESS_WAS:
        return u.cgi->HasQueryString();

    case RESOURCE_ADDRESS_NFS:
        return u.nfs->HasQueryString();
    }

    /* unreachable */
    assert(false);
    return false;
}

bool
ResourceAddress::IsExpandable() const
{
    switch (type) {
    case RESOURCE_ADDRESS_NONE:
        return false;

    case RESOURCE_ADDRESS_LOCAL:
        return u.file->IsExpandable();

    case RESOURCE_ADDRESS_PIPE:
    case RESOURCE_ADDRESS_CGI:
    case RESOURCE_ADDRESS_FASTCGI:
    case RESOURCE_ADDRESS_WAS:
        return u.cgi->IsExpandable();

    case RESOURCE_ADDRESS_HTTP:
    case RESOURCE_ADDRESS_AJP:
        return u.http->IsExpandable();

    case RESOURCE_ADDRESS_LHTTP:
        return u.lhttp->IsExpandable();

    case RESOURCE_ADDRESS_NFS:
        return u.nfs->IsExpandable();
    }

    assert(false);
    gcc_unreachable();
}

bool
ResourceAddress::Expand(struct pool &pool, const MatchInfo &match_info,
                        Error &error_r)
{
    switch (type) {
        struct file_address *file;
        struct cgi_address *cgi;
        struct http_address *uwa;
        LhttpAddress *lhttp;
        const struct nfs_address *nfs;

    case RESOURCE_ADDRESS_NONE:
        return true;

    case RESOURCE_ADDRESS_LOCAL:
        u.file = file = file_address_dup(pool, u.file);
        return file->Expand(&pool, match_info, error_r);

    case RESOURCE_ADDRESS_PIPE:
    case RESOURCE_ADDRESS_CGI:
    case RESOURCE_ADDRESS_FASTCGI:
    case RESOURCE_ADDRESS_WAS:
        u.cgi = cgi =
            cgi_address_dup(pool, u.cgi,
                            type == RESOURCE_ADDRESS_FASTCGI);
        return cgi->Expand(&pool, match_info, error_r);

    case RESOURCE_ADDRESS_HTTP:
    case RESOURCE_ADDRESS_AJP:
        /* copy the http_address object (it's a pointer, not
           in-line) and expand it */
        u.http = uwa = http_address_dup(pool, u.http);
        return uwa->Expand(&pool, match_info, error_r);

    case RESOURCE_ADDRESS_LHTTP:
        /* copy the lhttp_address object (it's a pointer, not
           in-line) and expand it */
        u.lhttp = lhttp = lhttp_address_dup(pool, u.lhttp);
        return lhttp->Expand(&pool, match_info, error_r);

    case RESOURCE_ADDRESS_NFS:
        /* copy the nfs_address object (it's a pointer, not
           in-line) and expand it */
        nfs = u.nfs->Expand(&pool, match_info, error_r);
        if (nfs == nullptr)
            return false;

        u.nfs = nfs;
        return true;
    }

    assert(false);
    gcc_unreachable();
}
