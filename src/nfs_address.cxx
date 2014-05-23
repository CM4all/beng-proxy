/*
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "nfs_address.hxx"
#include "uri_base.hxx"
#include "uri_escape.hxx"
#include "regex.hxx"
#include "pool.h"
#include "translate_client.hxx"

#include <assert.h>
#include <string.h>

nfs_address::nfs_address(struct pool *pool, const nfs_address &other)
    :server(p_strdup(pool, other.server)),
     export_name(p_strdup(pool, other.export_name)),
     path(p_strdup(pool, other.path)),
     expand_path(p_strdup_checked(pool, other.expand_path)),
     content_type(p_strdup_checked(pool, other.content_type)) {}

struct nfs_address *
nfs_address_new(struct pool *pool, const char *server,
                const char *export_name, const char *path)
{
    return NewFromPool<struct nfs_address>(pool,
                                           p_strdup(pool, server),
                                           p_strdup(pool, export_name),
                                           p_strdup(pool, path));
}

const char *
nfs_address_id(struct pool *pool, const struct nfs_address *address)
{
    assert(address != nullptr);
    assert(address->server != nullptr);
    assert(address->export_name != nullptr);
    assert(address->path != nullptr);

    return p_strcat(pool, address->server, ":", address->export_name, ":",
                    address->path, nullptr);
}

struct nfs_address *
nfs_address_dup(struct pool *pool, const struct nfs_address *src)
{
    return NewFromPool<struct nfs_address>(pool, pool, *src);
}

bool
nfs_address::Check(GError **error_r) const
{
    if (export_name == nullptr || *export_name == 0) {
        g_set_error_literal(error_r, translate_quark(), 0,
                            "missing NFS_EXPORT");
        return false;
    }

    if (path == nullptr || *path == 0) {
        g_set_error_literal(error_r, translate_quark(), 0,
                            "missing NFS PATH");
        return false;
    }

    return true;
}

struct nfs_address *
nfs_address_save_base(struct pool *pool, const struct nfs_address *src,
                      const char *suffix)
{
    assert(pool != nullptr);
    assert(src != nullptr);
    assert(suffix != nullptr);

    size_t length = base_string_unescape(pool, src->path, suffix);
    if (length == (size_t)-1)
        return nullptr;

    auto dest = NewFromPool<struct nfs_address>(pool,
                                                p_strdup(pool, src->server),
                                                p_strdup(pool,
                                                         src->export_name),
                                                p_strndup(pool,
                                                          src->path, length));
    dest->content_type = p_strdup_checked(pool, src->content_type);
    return dest;
}

struct nfs_address *
nfs_address_load_base(struct pool *pool, const struct nfs_address *src,
                      const char *suffix)
{
    assert(pool != nullptr);
    assert(src != nullptr);
    assert(src->path != nullptr);
    assert(*src->path != 0);
    assert(src->path[strlen(src->path) - 1] == '/');
    assert(suffix != nullptr);

    char *unescaped = uri_unescape_dup(pool, suffix, strlen(suffix));

    auto dest = NewFromPool<struct nfs_address>(pool,
                                                p_strdup(pool, src->server),
                                                p_strdup(pool,
                                                         src->export_name),
                                                p_strcat(pool, src->path,
                                                         unescaped, nullptr));
    dest->content_type = p_strdup_checked(pool, src->content_type);
    return dest;
}

const struct nfs_address *
nfs_address_expand(struct pool *pool, const struct nfs_address *src,
                   const GMatchInfo *match_info, GError **error_r)
{
    assert(pool != nullptr);
    assert(src != nullptr);
    assert(match_info != nullptr);

    if (src->expand_path == nullptr)
        return src;

    const char *path = expand_string_unescaped(pool, src->expand_path,
                                               match_info, error_r);
    if (path == nullptr)
        return nullptr;

    auto dest = NewFromPool<struct nfs_address>(pool,
                                                src->server,
                                                src->export_name,
                                                path);
    dest->content_type = p_strdup_checked(pool, src->content_type);
    return dest;
}
