/*
 * Address of a resource, which might be a local file, a CGI script or
 * a HTTP server.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "resource_address.hxx"
#include "file_address.hxx"
#include "lhttp_address.hxx"
#include "http_address.hxx"
#include "cgi_address.hxx"
#include "nfs_address.hxx"
#include "uri-relative.h"
#include "uri_edit.hxx"
#include "uri-extract.h"
#include "uri-verify.h"
#include "uri_base.hxx"
#include "strref.h"
#include "pool.hxx"
#include "http_quark.h"

#include <http/status.h>

void
resource_address_copy(struct pool &pool, struct resource_address *dest,
                      const struct resource_address *src)
{
    dest->type = src->type;

    switch (src->type) {
    case RESOURCE_ADDRESS_NONE:
        break;

    case RESOURCE_ADDRESS_LOCAL:
        assert(src->u.file != NULL);
        dest->u.file = file_address_dup(pool, src->u.file);
        break;

    case RESOURCE_ADDRESS_HTTP:
    case RESOURCE_ADDRESS_AJP:
        assert(src->u.http != NULL);
        dest->u.http = http_address_dup(pool, src->u.http);
        break;

    case RESOURCE_ADDRESS_LHTTP:
        assert(src->u.lhttp != NULL);
        dest->u.lhttp = lhttp_address_dup(pool, src->u.lhttp);
        break;

    case RESOURCE_ADDRESS_PIPE:
    case RESOURCE_ADDRESS_CGI:
    case RESOURCE_ADDRESS_FASTCGI:
    case RESOURCE_ADDRESS_WAS:
        dest->u.cgi = cgi_address_dup(pool, src->u.cgi,
                                      src->type == RESOURCE_ADDRESS_FASTCGI);
        break;

    case RESOURCE_ADDRESS_NFS:
        dest->u.nfs = nfs_address_dup(pool, src->u.nfs);
        break;
    }
}

struct resource_address *
resource_address_dup(struct pool &pool, const struct resource_address *src)
{
    auto dest = NewFromPool<struct resource_address>(pool);
    resource_address_copy(pool, dest, src);
    return dest;
}

struct resource_address *
resource_address_dup_with_path(struct pool &pool,
                               const struct resource_address *src,
                               const char *path)
{
    auto dest = NewFromPool<struct resource_address>(pool);
    dest->type = src->type;

    switch (src->type) {
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
        dest->u.http = http_address_dup_with_path(pool, src->u.http, path);
        break;

    case RESOURCE_ADDRESS_LHTTP:
        dest->u.lhttp = lhttp_address_dup_with_uri(pool, src->u.lhttp, path);
        break;
    }

    return dest;
}

const struct resource_address *
resource_address_insert_query_string_from(struct pool &pool,
                                          const struct resource_address *src,
                                          const char *uri)
{
    const char *query_string;
    struct resource_address *dest;

    switch (src->type) {
        struct cgi_address *cgi;

    case RESOURCE_ADDRESS_NONE:
    case RESOURCE_ADDRESS_LOCAL:
    case RESOURCE_ADDRESS_PIPE:
    case RESOURCE_ADDRESS_NFS:
        /* no query string support */
        return src;

    case RESOURCE_ADDRESS_HTTP:
    case RESOURCE_ADDRESS_AJP:
        assert(src->u.http != NULL);

        query_string = uri_query_string(uri);
        if (query_string == NULL)
            /* no query string in URI */
            return src;

        dest = NewFromPool<struct resource_address>(pool);
        dest->type = src->type;
        dest->u.http = src->u.http->InsertQueryString(pool, query_string);
        return dest;

    case RESOURCE_ADDRESS_LHTTP:
        assert(src->u.lhttp != NULL);

        query_string = uri_query_string(uri);
        if (query_string == NULL)
            /* no query string in URI */
            return src;

        dest = NewFromPool<struct resource_address>(pool);
        dest->type = src->type;
        dest->u.lhttp = src->u.lhttp->InsertQueryString(pool, query_string);
        return dest;

    case RESOURCE_ADDRESS_CGI:
    case RESOURCE_ADDRESS_FASTCGI:
    case RESOURCE_ADDRESS_WAS:
        assert(src->u.cgi->path != NULL);

        query_string = uri_query_string(uri);
        if (query_string == NULL)
            /* no query string in URI */
            return src;

        dest = resource_address_dup(pool, src);
        cgi = resource_address_get_cgi(dest);

        if (cgi->query_string != NULL)
            cgi->query_string = p_strcat(&pool, query_string, "&",
                                         cgi->query_string, NULL);
        else
            cgi->query_string = p_strdup(&pool, query_string);
        return dest;
    }

    assert(false);
    gcc_unreachable();
}

const struct resource_address *
resource_address_insert_args(struct pool &pool,
                             const struct resource_address *src,
                             const char *args, size_t args_length,
                             const char *path, size_t path_length)
{
    struct resource_address *dest;

    switch (src->type) {
        struct cgi_address *cgi;

    case RESOURCE_ADDRESS_NONE:
    case RESOURCE_ADDRESS_LOCAL:
    case RESOURCE_ADDRESS_PIPE:
    case RESOURCE_ADDRESS_NFS:
        /* no arguments support */
        return src;

    case RESOURCE_ADDRESS_HTTP:
    case RESOURCE_ADDRESS_AJP:
        assert(src->u.http != NULL);

        dest = NewFromPool<struct resource_address>(pool);
        dest->type = src->type;
        dest->u.http = src->u.http->InsertArgs(pool,
                                               args, args_length,
                                               path, path_length);
        return dest;

    case RESOURCE_ADDRESS_LHTTP:
        assert(src->u.lhttp != NULL);

        dest = NewFromPool<struct resource_address>(pool);
        dest->type = src->type;
        dest->u.lhttp = src->u.lhttp->InsertArgs(pool,
                                                 args, args_length,
                                                 path, path_length);
        return dest;

    case RESOURCE_ADDRESS_CGI:
    case RESOURCE_ADDRESS_FASTCGI:
    case RESOURCE_ADDRESS_WAS:
        assert(src->u.cgi->path != NULL);

        if (src->u.cgi->uri == NULL && src->u.cgi->path_info == NULL)
            return src;

        dest = resource_address_dup(pool, src);
        cgi = resource_address_get_cgi(dest);

        if (cgi->uri != NULL)
            cgi->uri = uri_insert_args(&pool, cgi->uri,
                                       args, args_length,
                                       path, path_length);

        if (cgi->path_info != NULL)
            cgi->path_info =
                p_strncat(&pool,
                          cgi->path_info, strlen(cgi->path_info),
                          ";", (size_t)1, args, args_length, path, path_length,
                          NULL);

        return dest;
    }

    assert(false);
    gcc_unreachable();
}

char *
resource_address_auto_base(struct pool *pool,
                           const struct resource_address *address,
                           const char *uri)
{
    assert(pool != NULL);
    assert(address != NULL);
    assert(uri != NULL);

    switch (address->type) {
    case RESOURCE_ADDRESS_NONE:
    case RESOURCE_ADDRESS_LOCAL:
    case RESOURCE_ADDRESS_PIPE:
    case RESOURCE_ADDRESS_HTTP:
    case RESOURCE_ADDRESS_LHTTP:
    case RESOURCE_ADDRESS_AJP:
    case RESOURCE_ADDRESS_NFS:
        return NULL;

    case RESOURCE_ADDRESS_CGI:
    case RESOURCE_ADDRESS_FASTCGI:
    case RESOURCE_ADDRESS_WAS:
        return address->u.cgi->AutoBase(pool, uri);
    }

    assert(false);
    gcc_unreachable();
}

struct resource_address *
resource_address_save_base(struct pool *pool, struct resource_address *dest,
                           const struct resource_address *src,
                           const char *suffix)
{
    assert(src != dest);

    switch (src->type) {
    case RESOURCE_ADDRESS_NONE:
    case RESOURCE_ADDRESS_PIPE:
        return NULL;

    case RESOURCE_ADDRESS_CGI:
    case RESOURCE_ADDRESS_FASTCGI:
    case RESOURCE_ADDRESS_WAS:
        dest->u.cgi = src->u.cgi->SaveBase(pool, suffix,
                                           src->type == RESOURCE_ADDRESS_FASTCGI);
        if (dest->u.cgi == NULL)
            return NULL;

        dest->type = src->type;
        return dest;

    case RESOURCE_ADDRESS_LOCAL:
        dest->u.file = src->u.file->SaveBase(pool, suffix);
        if (dest->u.file == NULL)
            return NULL;

        dest->type = src->type;
        return dest;

    case RESOURCE_ADDRESS_HTTP:
    case RESOURCE_ADDRESS_AJP:
        dest->u.http = src->u.http->SaveBase(pool, suffix);
        if (dest->u.http == NULL)
            return NULL;

        dest->type = src->type;
        return dest;

    case RESOURCE_ADDRESS_LHTTP:
        dest->u.lhttp = src->u.lhttp->SaveBase(pool, suffix);
        if (dest->u.lhttp == NULL)
            return NULL;

        dest->type = src->type;
        return dest;

    case RESOURCE_ADDRESS_NFS:
        dest->u.nfs = src->u.nfs->SaveBase(pool, suffix);
        if (dest->u.nfs == NULL)
            return NULL;

        dest->type = src->type;
        return dest;
    }

    assert(false);
    gcc_unreachable();
}

bool
resource_address::CacheStore(struct pool *pool,
                             const struct resource_address *src,
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
            resource_address_copy(*pool, this, src);
            return true;
        }

        if (src->type == RESOURCE_ADDRESS_NONE) {
            /* _save_base() will fail on a "NONE" address, but in this
               case, the operation is useful and is allowed as a
               special case */
            type = RESOURCE_ADDRESS_NONE;
            return true;
        }

        if (resource_address_save_base(pool, this, src, tail) != nullptr)
            return true;
    }

    resource_address_copy(*pool, this, src);
    return false;
}

struct resource_address *
resource_address_load_base(struct pool *pool, struct resource_address *dest,
                           const struct resource_address *src,
                           const char *suffix)
{
    switch (src->type) {
    case RESOURCE_ADDRESS_NONE:
    case RESOURCE_ADDRESS_PIPE:
        assert(false);
        gcc_unreachable();

    case RESOURCE_ADDRESS_CGI:
    case RESOURCE_ADDRESS_FASTCGI:
    case RESOURCE_ADDRESS_WAS:
        dest->type = src->type;
        dest->u.cgi = src->u.cgi->LoadBase(pool, suffix,
                                           src->type == RESOURCE_ADDRESS_FASTCGI);
        return dest;

    case RESOURCE_ADDRESS_LOCAL:
        dest->type = src->type;
        dest->u.file = src->u.file->LoadBase(pool, suffix);
        return dest;

    case RESOURCE_ADDRESS_HTTP:
    case RESOURCE_ADDRESS_AJP:
        dest->u.http = src->u.http->LoadBase(pool, suffix);
        if (dest->u.http == NULL)
            return NULL;

        dest->type = src->type;
        return dest;

    case RESOURCE_ADDRESS_LHTTP:
        dest->u.lhttp = src->u.lhttp->LoadBase(pool, suffix);
        if (dest->u.lhttp == NULL)
            return NULL;

        dest->type = src->type;
        return dest;

    case RESOURCE_ADDRESS_NFS:
        dest->u.nfs = src->u.nfs->LoadBase(pool, suffix);
        assert(dest->u.nfs != NULL);
        dest->type = src->type;
        return dest;
    }

    assert(false);
    gcc_unreachable();
}

bool
resource_address::CacheLoad(struct pool *pool,
                            const struct resource_address &src,
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

        if (resource_address_load_base(pool, this, &src, tail) != nullptr)
            return true;
    }

    resource_address_copy(*pool, this, &src);
    return true;
}

const struct resource_address *
resource_address_apply(struct pool *pool, const struct resource_address *src,
                       const char *relative, size_t relative_length,
                       struct resource_address *buffer)
{
    const struct http_address *uwa;
    const struct cgi_address *cgi;
    const struct lhttp_address *lhttp;

    assert(pool != NULL);
    assert(src != NULL);
    assert(relative != NULL || relative_length == 0);

    switch (src->type) {
    case RESOURCE_ADDRESS_NONE:
        return NULL;

    case RESOURCE_ADDRESS_LOCAL:
    case RESOURCE_ADDRESS_PIPE:
    case RESOURCE_ADDRESS_NFS:
        return src;

    case RESOURCE_ADDRESS_HTTP:
    case RESOURCE_ADDRESS_AJP:
        uwa = src->u.http->Apply(pool, relative, relative_length);
        if (uwa == NULL)
            return NULL;

        if (uwa == src->u.http)
            return src;

        buffer->type = src->type;
        buffer->u.http = uwa;
        return buffer;

    case RESOURCE_ADDRESS_LHTTP:
        lhttp = src->u.lhttp->Apply(pool, relative, relative_length);
        if (lhttp == NULL)
            return NULL;

        if (lhttp == src->u.lhttp)
            return src;

        buffer->type = src->type;
        buffer->u.lhttp = lhttp;
        return buffer;

    case RESOURCE_ADDRESS_CGI:
    case RESOURCE_ADDRESS_FASTCGI:
    case RESOURCE_ADDRESS_WAS:
        cgi = src->u.cgi->Apply(pool, relative, relative_length,
                                src->type == RESOURCE_ADDRESS_FASTCGI);
        if (cgi == NULL)
            return NULL;

        if (cgi == src->u.cgi)
            return src;

        buffer->type = src->type;
        buffer->u.cgi = cgi;
        return buffer;
    }

    assert(false);
    gcc_unreachable();
}

const struct strref *
resource_address_relative(const struct resource_address *base,
                          const struct resource_address *address,
                          struct strref *buffer)
{
    struct strref base_uri;

    assert(base != NULL);
    assert(address != NULL);
    assert(base->type == address->type);
    assert(buffer != NULL);

    switch (address->type) {
    case RESOURCE_ADDRESS_NONE:
    case RESOURCE_ADDRESS_LOCAL:
    case RESOURCE_ADDRESS_PIPE:
    case RESOURCE_ADDRESS_NFS:
        return NULL;

    case RESOURCE_ADDRESS_HTTP:
    case RESOURCE_ADDRESS_AJP:
        return http_address_relative(base->u.http, address->u.http, buffer);

    case RESOURCE_ADDRESS_LHTTP:
        return lhttp_address_relative(base->u.lhttp, address->u.lhttp, buffer);

    case RESOURCE_ADDRESS_CGI:
    case RESOURCE_ADDRESS_FASTCGI:
    case RESOURCE_ADDRESS_WAS:
        strref_set_c(&base_uri, base->u.cgi->path_info != NULL
                     ? base->u.cgi->path_info : "");
        strref_set_c(buffer, address->u.cgi->path_info != NULL
                     ? address->u.cgi->path_info : "");
        return uri_relative(&base_uri, buffer);
    }

    assert(false);
    gcc_unreachable();
}

const char *
resource_address_id(const struct resource_address *address, struct pool *pool)
{
    switch (address->type) {
    case RESOURCE_ADDRESS_NONE:
        return "";

    case RESOURCE_ADDRESS_LOCAL:
        return p_strdup(pool, address->u.file->path);

    case RESOURCE_ADDRESS_HTTP:
    case RESOURCE_ADDRESS_AJP:
        return address->u.http->GetAbsoluteURI(pool);

    case RESOURCE_ADDRESS_LHTTP:
        return address->u.lhttp->GetId(pool);

    case RESOURCE_ADDRESS_PIPE:
    case RESOURCE_ADDRESS_CGI:
    case RESOURCE_ADDRESS_FASTCGI:
    case RESOURCE_ADDRESS_WAS:
        return address->u.cgi->GetId(pool);

    case RESOURCE_ADDRESS_NFS:
        return address->u.nfs->GetId(pool);
    }

    assert(false);
    gcc_unreachable();
}

const char *
resource_address_host_and_port(const struct resource_address *address)
{
    assert(address != NULL);

    switch (address->type) {
    case RESOURCE_ADDRESS_NONE:
    case RESOURCE_ADDRESS_LOCAL:
    case RESOURCE_ADDRESS_PIPE:
    case RESOURCE_ADDRESS_CGI:
    case RESOURCE_ADDRESS_FASTCGI:
    case RESOURCE_ADDRESS_WAS:
    case RESOURCE_ADDRESS_NFS:
        return NULL;

    case RESOURCE_ADDRESS_HTTP:
    case RESOURCE_ADDRESS_AJP:
        return address->u.http->host_and_port;

    case RESOURCE_ADDRESS_LHTTP:
        return address->u.lhttp->host_and_port;
    }

    assert(false);
    gcc_unreachable();
}

const char *
resource_address_uri_path(const struct resource_address *address)
{
    assert(address != NULL);

    switch (address->type) {
    case RESOURCE_ADDRESS_NONE:
    case RESOURCE_ADDRESS_LOCAL:
    case RESOURCE_ADDRESS_PIPE:
    case RESOURCE_ADDRESS_NFS:
        return NULL;

    case RESOURCE_ADDRESS_HTTP:
    case RESOURCE_ADDRESS_AJP:
        return address->u.http->path;

    case RESOURCE_ADDRESS_LHTTP:
        return address->u.lhttp->uri;

    case RESOURCE_ADDRESS_CGI:
    case RESOURCE_ADDRESS_FASTCGI:
    case RESOURCE_ADDRESS_WAS:
        if (address->u.cgi->uri != NULL)
            return address->u.cgi->uri;

        return address->u.cgi->script_name;
    }

    assert(false);
    gcc_unreachable();
}

bool
resource_address::Check(GError **error_r) const
{
    switch (type) {
    case RESOURCE_ADDRESS_NONE:
    case RESOURCE_ADDRESS_LOCAL:
    case RESOURCE_ADDRESS_HTTP:
        return true;

    case RESOURCE_ADDRESS_LHTTP:
        return u.lhttp->Check(error_r);

    case RESOURCE_ADDRESS_PIPE:
    case RESOURCE_ADDRESS_CGI:
    case RESOURCE_ADDRESS_FASTCGI:
    case RESOURCE_ADDRESS_WAS:
    case RESOURCE_ADDRESS_AJP:
        return true;

    case RESOURCE_ADDRESS_NFS:
        return u.nfs->Check(error_r);
    }

    return true;
}

bool
resource_address::IsValidBase() const
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
resource_address::HasQueryString() const
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
resource_address_is_expandable(const struct resource_address *address)
{
    assert(address != NULL);

    switch (address->type) {
    case RESOURCE_ADDRESS_NONE:
        return false;

    case RESOURCE_ADDRESS_LOCAL:
        return address->u.file->IsExpandable();

    case RESOURCE_ADDRESS_PIPE:
    case RESOURCE_ADDRESS_CGI:
    case RESOURCE_ADDRESS_FASTCGI:
    case RESOURCE_ADDRESS_WAS:
        return address->u.cgi->IsExpandable();

    case RESOURCE_ADDRESS_HTTP:
    case RESOURCE_ADDRESS_AJP:
        return address->u.http->IsExpandable();

    case RESOURCE_ADDRESS_LHTTP:
        return address->u.lhttp->IsExpandable();

    case RESOURCE_ADDRESS_NFS:
        return address->u.nfs->IsExpandable();
    }

    assert(false);
    gcc_unreachable();
}

bool
resource_address_expand(struct pool *pool, struct resource_address *address,
                        const GMatchInfo *match_info, GError **error_r)
{
    assert(pool != NULL);
    assert(address != NULL);
    assert(match_info != NULL);

    switch (address->type) {
        struct file_address *file;
        struct cgi_address *cgi;
        struct http_address *uwa;
        struct lhttp_address *lhttp;
        const struct nfs_address *nfs;

    case RESOURCE_ADDRESS_NONE:
        return true;

    case RESOURCE_ADDRESS_LOCAL:
        address->u.file = file = file_address_dup(*pool, address->u.file);
        return file->Expand(pool, match_info, error_r);

    case RESOURCE_ADDRESS_PIPE:
    case RESOURCE_ADDRESS_CGI:
    case RESOURCE_ADDRESS_FASTCGI:
    case RESOURCE_ADDRESS_WAS:
        address->u.cgi = cgi =
            cgi_address_dup(*pool, address->u.cgi,
                            address->type == RESOURCE_ADDRESS_FASTCGI);
        return cgi->Expand(pool, match_info, error_r);

    case RESOURCE_ADDRESS_HTTP:
    case RESOURCE_ADDRESS_AJP:
        /* copy the http_address object (it's a pointer, not
           in-line) and expand it */
        address->u.http = uwa = http_address_dup(*pool, address->u.http);
        return uwa->Expand(pool, match_info, error_r);

    case RESOURCE_ADDRESS_LHTTP:
        /* copy the lhttp_address object (it's a pointer, not
           in-line) and expand it */
        address->u.lhttp = lhttp = lhttp_address_dup(*pool, address->u.lhttp);
        return lhttp->Expand(pool, match_info, error_r);

    case RESOURCE_ADDRESS_NFS:
        /* copy the nfs_address object (it's a pointer, not
           in-line) and expand it */
        nfs = address->u.nfs->Expand(pool, match_info, error_r);
        if (nfs == NULL)
            return false;

        address->u.nfs = nfs;
        return true;
    }

    assert(false);
    gcc_unreachable();
}
