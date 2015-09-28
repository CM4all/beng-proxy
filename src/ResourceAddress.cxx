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
#include "uri/uri_relative.hxx"
#include "uri/uri_extract.hxx"
#include "uri/uri_verify.hxx"
#include "uri/uri_base.hxx"
#include "puri_edit.hxx"
#include "pool.hxx"
#include "http_quark.h"
#include "util/StringView.hxx"

#include <http/status.h>

void
ResourceAddress::CopyFrom(struct pool &pool, const ResourceAddress &src)
{
    type = src.type;

    switch (src.type) {
    case Type::NONE:
        break;

    case Type::LOCAL:
        assert(src.u.file != nullptr);
        u.file = file_address_dup(pool, src.u.file);
        break;

    case Type::HTTP:
    case Type::AJP:
        assert(src.u.http != nullptr);
        u.http = http_address_dup(pool, src.u.http);
        break;

    case Type::LHTTP:
        assert(src.u.lhttp != nullptr);
        u.lhttp = src.u.lhttp->Dup(pool);
        break;

    case Type::PIPE:
    case Type::CGI:
    case Type::FASTCGI:
    case Type::WAS:
        u.cgi = cgi_address_dup(pool, src.u.cgi,
                                src.type == Type::FASTCGI);
        break;

    case Type::NFS:
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
    case Type::NONE:
    case Type::LOCAL:
    case Type::PIPE:
    case Type::NFS:
    case Type::CGI:
    case Type::FASTCGI:
    case Type::WAS:
        assert(false);
        gcc_unreachable();

    case Type::HTTP:
    case Type::AJP:
        dest->u.http = http_address_dup_with_path(pool, u.http, path);
        break;

    case Type::LHTTP:
        dest->u.lhttp = u.lhttp->DupWithUri(pool, path);
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

    case Type::NONE:
    case Type::LOCAL:
    case Type::PIPE:
    case Type::NFS:
        /* no query string support */
        return this;

    case Type::HTTP:
    case Type::AJP:
        assert(u.http != nullptr);

        query_string = uri_query_string(uri);
        if (query_string == nullptr)
            /* no query string in URI */
            return this;

        dest = NewFromPool<ResourceAddress>(pool);
        dest->type = type;
        dest->u.http = u.http->InsertQueryString(pool, query_string);
        return dest;

    case Type::LHTTP:
        assert(u.lhttp != nullptr);

        query_string = uri_query_string(uri);
        if (query_string == nullptr)
            /* no query string in URI */
            return this;

        dest = NewFromPool<ResourceAddress>(pool);
        dest->type = type;
        dest->u.lhttp = u.lhttp->InsertQueryString(pool, query_string);
        return dest;

    case Type::CGI:
    case Type::FASTCGI:
    case Type::WAS:
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

    case Type::NONE:
    case Type::LOCAL:
    case Type::PIPE:
    case Type::NFS:
        /* no arguments support */
        return this;

    case Type::HTTP:
    case Type::AJP:
        assert(u.http != nullptr);

        dest = NewFromPool<ResourceAddress>(pool);
        dest->type = type;
        dest->u.http = u.http->InsertArgs(pool,
                                               args, args_length,
                                               path, path_length);
        return dest;

    case Type::LHTTP:
        assert(u.lhttp != nullptr);

        dest = NewFromPool<ResourceAddress>(pool);
        dest->type = type;
        dest->u.lhttp = u.lhttp->InsertArgs(pool,
                                                 args, args_length,
                                                 path, path_length);
        return dest;

    case Type::CGI:
    case Type::FASTCGI:
    case Type::WAS:
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
    case Type::NONE:
    case Type::LOCAL:
    case Type::PIPE:
    case Type::HTTP:
    case Type::LHTTP:
    case Type::AJP:
    case Type::NFS:
        return nullptr;

    case Type::CGI:
    case Type::FASTCGI:
    case Type::WAS:
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
    case Type::NONE:
    case Type::PIPE:
        return nullptr;

    case Type::CGI:
    case Type::FASTCGI:
    case Type::WAS:
        dest.u.cgi = u.cgi->SaveBase(&pool, suffix,
                                     type == Type::FASTCGI);
        if (dest.u.cgi == nullptr)
            return nullptr;

        dest.type = type;
        return &dest;

    case Type::LOCAL:
        dest.u.file = u.file->SaveBase(&pool, suffix);
        if (dest.u.file == nullptr)
            return nullptr;

        dest.type = type;
        return &dest;

    case Type::HTTP:
    case Type::AJP:
        dest.u.http = u.http->SaveBase(&pool, suffix);
        if (dest.u.http == nullptr)
            return nullptr;

        dest.type = type;
        return &dest;

    case Type::LHTTP:
        dest.u.lhttp = u.lhttp->SaveBase(&pool, suffix);
        if (dest.u.lhttp == nullptr)
            return nullptr;

        dest.type = type;
        return &dest;

    case Type::NFS:
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

        if (src->type == Type::NONE) {
            /* _save_base() will fail on a "NONE" address, but in this
               case, the operation is useful and is allowed as a
               special case */
            type = Type::NONE;
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
    case Type::NONE:
    case Type::PIPE:
        assert(false);
        gcc_unreachable();

    case Type::CGI:
    case Type::FASTCGI:
    case Type::WAS:
        dest.type = type;
        dest.u.cgi = u.cgi->LoadBase(&pool, suffix,
                                     type == Type::FASTCGI);
        if (dest.u.cgi == nullptr)
            return nullptr;

        return &dest;

    case Type::LOCAL:
        dest.type = type;
        dest.u.file = u.file->LoadBase(&pool, suffix);
        if (dest.u.file == nullptr)
            return nullptr;

        return &dest;

    case Type::HTTP:
    case Type::AJP:
        dest.u.http = u.http->LoadBase(&pool, suffix);
        if (dest.u.http == nullptr)
            return nullptr;

        dest.type = type;
        return &dest;

    case Type::LHTTP:
        dest.u.lhttp = u.lhttp->LoadBase(&pool, suffix);
        if (dest.u.lhttp == nullptr)
            return nullptr;

        dest.type = type;
        return &dest;

    case Type::NFS:
        dest.u.nfs = u.nfs->LoadBase(&pool, suffix);
        if (dest.u.nfs == nullptr)
            return nullptr;

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

        if (src.type == Type::NONE) {
            /* see code comment in tcache_store_address() */
            type = Type::NONE;
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
    const HttpAddress *uwa;
    const struct cgi_address *cgi;
    const LhttpAddress *lhttp;

    assert(relative != nullptr || relative_length == 0);

    switch (type) {
    case Type::NONE:
        return nullptr;

    case Type::LOCAL:
    case Type::PIPE:
    case Type::NFS:
        return this;

    case Type::HTTP:
    case Type::AJP:
        uwa = u.http->Apply(&pool, relative, relative_length);
        if (uwa == nullptr)
            return nullptr;

        if (uwa == u.http)
            return this;

        buffer.type = type;
        buffer.u.http = uwa;
        return &buffer;

    case Type::LHTTP:
        lhttp = u.lhttp->Apply(&pool, relative, relative_length);
        if (lhttp == nullptr)
            return nullptr;

        if (lhttp == u.lhttp)
            return this;

        buffer.type = type;
        buffer.u.lhttp = lhttp;
        return &buffer;

    case Type::CGI:
    case Type::FASTCGI:
    case Type::WAS:
        cgi = u.cgi->Apply(&pool, relative, relative_length,
                           type == Type::FASTCGI);
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

StringView
ResourceAddress::RelativeTo(const ResourceAddress &base) const
{
    assert(base.type == type);

    StringView buffer;

    switch (type) {
    case Type::NONE:
    case Type::LOCAL:
    case Type::PIPE:
    case Type::NFS:
        return nullptr;

    case Type::HTTP:
    case Type::AJP:
        return http_address_relative(base.u.http, u.http);

    case Type::LHTTP:
        return u.lhttp->RelativeTo(*base.u.lhttp);

    case Type::CGI:
    case Type::FASTCGI:
    case Type::WAS:
        buffer = u.cgi->path_info;
        return uri_relative(base.u.cgi->path_info, buffer);
    }

    assert(false);
    gcc_unreachable();
}

const char *
ResourceAddress::GetId(struct pool &pool) const
{
    switch (type) {
    case Type::NONE:
        return "";

    case Type::LOCAL:
        return p_strdup(&pool, u.file->path);

    case Type::HTTP:
    case Type::AJP:
        return u.http->GetAbsoluteURI(&pool);

    case Type::LHTTP:
        return u.lhttp->GetId(&pool);

    case Type::PIPE:
    case Type::CGI:
    case Type::FASTCGI:
    case Type::WAS:
        return u.cgi->GetId(&pool);

    case Type::NFS:
        return u.nfs->GetId(&pool);
    }

    assert(false);
    gcc_unreachable();
}

const char *
ResourceAddress::GetHostAndPort() const
{
    switch (type) {
    case Type::NONE:
    case Type::LOCAL:
    case Type::PIPE:
    case Type::CGI:
    case Type::FASTCGI:
    case Type::WAS:
    case Type::NFS:
        return nullptr;

    case Type::HTTP:
    case Type::AJP:
        return u.http->host_and_port;

    case Type::LHTTP:
        return u.lhttp->host_and_port;
    }

    assert(false);
    gcc_unreachable();
}

const char *
ResourceAddress::GetUriPath() const
{
    switch (type) {
    case Type::NONE:
    case Type::LOCAL:
    case Type::PIPE:
    case Type::NFS:
        return nullptr;

    case Type::HTTP:
    case Type::AJP:
        return u.http->path;

    case Type::LHTTP:
        return u.lhttp->uri;

    case Type::CGI:
    case Type::FASTCGI:
    case Type::WAS:
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
    case Type::NONE:
    case Type::HTTP:
        return true;

    case Type::LOCAL:
        return u.file->Check(error_r);

    case Type::LHTTP:
        return u.lhttp->Check(error_r);

    case Type::PIPE:
    case Type::CGI:
    case Type::FASTCGI:
    case Type::WAS:
        return u.cgi->Check(error_r);

    case Type::AJP:
        return true;

    case Type::NFS:
        return u.nfs->Check(error_r);
    }

    return true;
}

bool
ResourceAddress::IsValidBase() const
{
    switch (type) {
    case Type::NONE:
        return true;

    case Type::LOCAL:
        return u.file->IsValidBase();

    case Type::HTTP:
    case Type::AJP:
        return u.http->IsValidBase();

    case Type::LHTTP:
        return u.lhttp->IsValidBase();

    case Type::PIPE:
    case Type::CGI:
    case Type::FASTCGI:
    case Type::WAS:
        return u.cgi->IsValidBase();

    case Type::NFS:
        return u.nfs->IsValidBase();
    }

    assert(false);
    gcc_unreachable();
}

bool
ResourceAddress::HasQueryString() const
{
    switch (type) {
    case Type::NONE:
        return false;

    case Type::LOCAL:
        return u.file->HasQueryString();

    case Type::HTTP:
    case Type::AJP:
        return u.http->HasQueryString();

    case Type::LHTTP:
        return u.lhttp->HasQueryString();

    case Type::PIPE:
    case Type::CGI:
    case Type::FASTCGI:
    case Type::WAS:
        return u.cgi->HasQueryString();

    case Type::NFS:
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
    case Type::NONE:
        return false;

    case Type::LOCAL:
        return u.file->IsExpandable();

    case Type::PIPE:
    case Type::CGI:
    case Type::FASTCGI:
    case Type::WAS:
        return u.cgi->IsExpandable();

    case Type::HTTP:
    case Type::AJP:
        return u.http->IsExpandable();

    case Type::LHTTP:
        return u.lhttp->IsExpandable();

    case Type::NFS:
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
        HttpAddress *uwa;
        LhttpAddress *lhttp;
        const struct nfs_address *nfs;

    case Type::NONE:
        return true;

    case Type::LOCAL:
        u.file = file = file_address_dup(pool, u.file);
        return file->Expand(&pool, match_info, error_r);

    case Type::PIPE:
    case Type::CGI:
    case Type::FASTCGI:
    case Type::WAS:
        u.cgi = cgi =
            cgi_address_dup(pool, u.cgi,
                            type == Type::FASTCGI);
        return cgi->Expand(&pool, match_info, error_r);

    case Type::HTTP:
    case Type::AJP:
        /* copy the http_address object (it's a pointer, not
           in-line) and expand it */
        u.http = uwa = http_address_dup(pool, u.http);
        return uwa->Expand(&pool, match_info, error_r);

    case Type::LHTTP:
        /* copy the lhttp_address object (it's a pointer, not
           in-line) and expand it */
        u.lhttp = lhttp = u.lhttp->Dup(pool);
        return lhttp->Expand(&pool, match_info, error_r);

    case Type::NFS:
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
