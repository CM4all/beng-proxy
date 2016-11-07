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
#include "pool.hxx"
#include "HttpMessageResponse.hxx"
#include "util/StringView.hxx"

#include <http/status.h>

bool
ResourceAddress::IsHttp() const
{
    return type == Type::HTTP &&
        GetHttp().protocol == HttpAddress::Protocol::HTTP;
}

void
ResourceAddress::CopyFrom(struct pool &pool, const ResourceAddress &src)
{
    type = src.type;

    switch (src.type) {
    case Type::NONE:
        break;

    case Type::LOCAL:
        assert(src.u.file != nullptr);
        u.file = NewFromPool<FileAddress>(pool, &pool, *src.u.file);
        break;

    case Type::HTTP:
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
        u.cgi = src.u.cgi->Clone(pool);
        break;

    case Type::NFS:
        u.nfs = nfs_address_dup(pool, src.u.nfs);
        break;
    }
}

ResourceAddress *
ResourceAddress::Dup(struct pool &pool) const
{
    return NewFromPool<ResourceAddress>(pool, pool, *this);
}

ResourceAddress
ResourceAddress::WithPath(struct pool &pool, const char *path) const
{
    switch (type) {
    case Type::NONE:
    case Type::LOCAL:
    case Type::PIPE:
    case Type::NFS:
    case Type::CGI:
    case Type::FASTCGI:
    case Type::WAS:
        break;

    case Type::HTTP:
        return *NewFromPool<HttpAddress>(pool, ShallowCopy(), GetHttp(), path);

    case Type::LHTTP:
        return *NewFromPool<LhttpAddress>(pool, ShallowCopy(),
                                          GetLhttp(), path);
    }

    assert(false);
    gcc_unreachable();
}

ResourceAddress
ResourceAddress::WithQueryStringFrom(struct pool &pool, const char *uri) const
{
    const char *query_string;

    switch (type) {
        CgiAddress *cgi;

    case Type::NONE:
    case Type::LOCAL:
    case Type::PIPE:
    case Type::NFS:
        /* no query string support */
        return {ShallowCopy(), *this};

    case Type::HTTP:
        assert(u.http != nullptr);

        query_string = uri_query_string(uri);
        if (query_string == nullptr)
            /* no query string in URI */
            return {ShallowCopy(), *this};

        return *u.http->InsertQueryString(pool, query_string);

    case Type::LHTTP:
        assert(u.lhttp != nullptr);

        query_string = uri_query_string(uri);
        if (query_string == nullptr)
            /* no query string in URI */
            return {ShallowCopy(), *this};

        return *u.lhttp->InsertQueryString(pool, query_string);

    case Type::CGI:
    case Type::FASTCGI:
    case Type::WAS:
        assert(u.cgi->path != nullptr);

        query_string = uri_query_string(uri);
        if (query_string == nullptr)
            /* no query string in URI */
            return {ShallowCopy(), *this};

        cgi = NewFromPool<CgiAddress>(pool, ShallowCopy(), GetCgi());
        cgi->InsertQueryString(pool, query_string);
        return ResourceAddress(type, *cgi);
    }

    assert(false);
    gcc_unreachable();
}

ResourceAddress
ResourceAddress::WithArgs(struct pool &pool,
                          StringView args, StringView path) const
{
    switch (type) {
        CgiAddress *cgi;

    case Type::NONE:
    case Type::LOCAL:
    case Type::PIPE:
    case Type::NFS:
        /* no arguments support */
        return {ShallowCopy(), *this};

    case Type::HTTP:
        assert(u.http != nullptr);

        return *GetHttp().InsertArgs(pool, args, path);

    case Type::LHTTP:
        assert(u.lhttp != nullptr);

        return *GetLhttp().InsertArgs(pool, args, path);

    case Type::CGI:
    case Type::FASTCGI:
    case Type::WAS:
        assert(u.cgi->path != nullptr);

        if (u.cgi->uri == nullptr && u.cgi->path_info == nullptr)
            return {ShallowCopy(), *this};

        cgi = NewFromPool<CgiAddress>(pool, ShallowCopy(), GetCgi());
        cgi->InsertArgs(pool, args, path);
        return ResourceAddress(type, *cgi);
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

ResourceAddress
ResourceAddress::SaveBase(struct pool &pool, const char *suffix) const
{
    switch (type) {
    case Type::NONE:
    case Type::PIPE:
        return nullptr;

    case Type::CGI:
    case Type::FASTCGI:
    case Type::WAS:
        {
            auto *cgi = GetCgi().SaveBase(&pool, suffix);
            if (cgi == nullptr)
                return nullptr;

            return ResourceAddress(type, *cgi);
        }

    case Type::LOCAL:
        {
            auto *file = GetFile().SaveBase(&pool, suffix);
            if (file == nullptr)
                return nullptr;

            return *file;
        }

    case Type::HTTP:
        {
            auto *http = GetHttp().SaveBase(&pool, suffix);
            if (http == nullptr)
                return nullptr;

            return *http;
        }

    case Type::LHTTP:
        {
            auto *lhttp = GetLhttp().SaveBase(&pool, suffix);
            if (lhttp == nullptr)
                return nullptr;

            return *lhttp;
        }

    case Type::NFS:
        {
            auto *nfs = GetNfs().SaveBase(&pool, suffix);
            if (nfs == nullptr)
                return nullptr;

            return *nfs;
        }
    }

    assert(false);
    gcc_unreachable();
}

bool
ResourceAddress::CacheStore(struct pool &pool,
                            const ResourceAddress &src,
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
            CopyFrom(pool, src);
            return true;
        }

        if (src.type == Type::NONE) {
            /* _save_base() will fail on a "NONE" address, but in this
               case, the operation is useful and is allowed as a
               special case */
            type = Type::NONE;
            return true;
        }

        *this = src.SaveBase(pool, tail);
        if (IsDefined())
            return true;
    }

    CopyFrom(pool, src);
    return false;
}

ResourceAddress
ResourceAddress::LoadBase(struct pool &pool, const char *suffix) const
{
    switch (type) {
    case Type::NONE:
    case Type::PIPE:
        assert(false);
        gcc_unreachable();

    case Type::CGI:
    case Type::FASTCGI:
    case Type::WAS:
        {
            auto *cgi = GetCgi().LoadBase(&pool, suffix);
            if (cgi == nullptr)
                return nullptr;

            return ResourceAddress(type, *cgi);
        }

    case Type::LOCAL:
        {
            auto *file = GetFile().LoadBase(&pool, suffix);
            if (file == nullptr)
                return nullptr;

            return *file;
        }

    case Type::HTTP:
        {
            auto *http = GetHttp().LoadBase(&pool, suffix);
            if (http == nullptr)
                return nullptr;

            return *http;
        }

    case Type::LHTTP:
        {
            auto *lhttp = GetLhttp().LoadBase(&pool, suffix);
            if (lhttp == nullptr)
                return nullptr;

            return *lhttp;
        }

    case Type::NFS:
        {
            auto *nfs = GetNfs().LoadBase(&pool, suffix);
            if (nfs == nullptr)
                return nullptr;

            return *nfs;
        }
    }

    assert(false);
    gcc_unreachable();
}

void
ResourceAddress::CacheLoad(struct pool &pool, const ResourceAddress &src,
                           const char *uri, const char *base,
                           bool unsafe_base, bool expandable)
{
    if (base != nullptr && !expandable) {
        const char *tail = require_base_tail(uri, base);

        if (!unsafe_base && !uri_path_verify_paranoid(tail - 1))
            throw HttpMessageResponse(HTTP_STATUS_BAD_REQUEST, "Malformed URI");

        if (src.type == Type::NONE) {
            /* see code comment in tcache_store_address() */
            type = Type::NONE;
            return;
        }

        *this = src.LoadBase(pool, tail);
        if (IsDefined())
            return;
    }

    CopyFrom(pool, src);
}

ResourceAddress
ResourceAddress::Apply(struct pool &pool, StringView relative) const
{
    const HttpAddress *uwa;
    const CgiAddress *cgi;
    const LhttpAddress *lhttp;

    switch (type) {
    case Type::NONE:
        return nullptr;

    case Type::LOCAL:
    case Type::PIPE:
    case Type::NFS:
        return {ShallowCopy(), *this};

    case Type::HTTP:
        uwa = u.http->Apply(&pool, relative);
        if (uwa == nullptr)
            return nullptr;

        return *uwa;

    case Type::LHTTP:
        lhttp = u.lhttp->Apply(&pool, relative);
        if (lhttp == nullptr)
            return nullptr;

        return *lhttp;

    case Type::CGI:
    case Type::FASTCGI:
    case Type::WAS:
        cgi = u.cgi->Apply(&pool, relative);
        if (cgi == nullptr)
            return nullptr;

        return ResourceAddress(type, *cgi);
    }

    assert(false);
    gcc_unreachable();
}

StringView
ResourceAddress::RelativeTo(const ResourceAddress &base) const
{
    assert(base.type == type);

    switch (type) {
    case Type::NONE:
    case Type::LOCAL:
    case Type::PIPE:
    case Type::NFS:
        return nullptr;

    case Type::HTTP:
        return u.http->RelativeTo(*base.u.http);

    case Type::LHTTP:
        return u.lhttp->RelativeTo(*base.u.lhttp);

    case Type::CGI:
    case Type::FASTCGI:
    case Type::WAS:
        return uri_relative(base.u.cgi->path_info, u.cgi->path_info);
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

void
ResourceAddress::Check() const
{
    switch (type) {
    case Type::NONE:
        break;

    case Type::HTTP:
        u.http->Check();
        break;

    case Type::LOCAL:
        u.file->Check();
        break;

    case Type::LHTTP:
        u.lhttp->Check();
        break;

    case Type::PIPE:
    case Type::CGI:
    case Type::FASTCGI:
    case Type::WAS:
        u.cgi->Check();
        break;

    case Type::NFS:
        u.nfs->Check();
        break;
    }
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
        return u.http->IsExpandable();

    case Type::LHTTP:
        return u.lhttp->IsExpandable();

    case Type::NFS:
        return u.nfs->IsExpandable();
    }

    assert(false);
    gcc_unreachable();
}

void
ResourceAddress::Expand(struct pool &pool, const MatchInfo &match_info)
{
    switch (type) {
        FileAddress *file;
        CgiAddress *cgi;
        HttpAddress *uwa;
        LhttpAddress *lhttp;

    case Type::NONE:
        break;

    case Type::LOCAL:
        u.file = file = NewFromPool<FileAddress>(pool, &pool, *u.file);
        file->Expand(&pool, match_info);
        break;

    case Type::PIPE:
    case Type::CGI:
    case Type::FASTCGI:
    case Type::WAS:
        u.cgi = cgi = u.cgi->Clone(pool);
        cgi->Expand(&pool, match_info);
        break;

    case Type::HTTP:
        /* copy the http_address object (it's a pointer, not
           in-line) and expand it */
        u.http = uwa = http_address_dup(pool, u.http);
        uwa->Expand(&pool, match_info);
        break;

    case Type::LHTTP:
        /* copy the lhttp_address object (it's a pointer, not
           in-line) and expand it */
        u.lhttp = lhttp = u.lhttp->Dup(pool);
        lhttp->Expand(&pool, match_info);
        break;

    case Type::NFS:
        /* copy the nfs_address object (it's a pointer, not
           in-line) and expand it */
        u.nfs = u.nfs->Expand(&pool, match_info);
        break;
    }
}
