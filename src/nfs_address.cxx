/*
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "nfs_address.hxx"
#include "uri_base.hxx"
#include "uri_escape.hxx"
#include "regex.hxx"
#include "pool.hxx"
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
nfs_address_new(struct pool &pool, const char *server,
                const char *export_name, const char *path)
{
    return NewFromPool<struct nfs_address>(pool,
                                           p_strdup(&pool, server),
                                           p_strdup(&pool, export_name),
                                           p_strdup(&pool, path));
}

const char *
nfs_address::GetId(struct pool *pool) const
{
    assert(server != nullptr);
    assert(export_name != nullptr);
    assert(path != nullptr);

    return p_strcat(pool, server, ":", export_name, ":", path, nullptr);
}

struct nfs_address *
nfs_address_dup(struct pool &pool, const struct nfs_address *src)
{
    return NewFromPool<struct nfs_address>(pool, &pool, *src);
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

bool
nfs_address::IsValidBase() const
{
    return IsExpandable() || is_base(path);
}

struct nfs_address *
nfs_address::SaveBase(struct pool *pool, const char *suffix) const
{
    assert(pool != nullptr);
    assert(suffix != nullptr);

    size_t length = base_string_unescape(pool, path, suffix);
    if (length == (size_t)-1)
        return nullptr;

    auto dest = NewFromPool<struct nfs_address>(*pool,
                                                p_strdup(pool, server),
                                                p_strdup(pool, export_name),
                                                p_strndup(pool, path, length));
    dest->content_type = p_strdup_checked(pool, content_type);
    return dest;
}

struct nfs_address *
nfs_address::LoadBase(struct pool *pool, const char *suffix) const
{
    assert(pool != nullptr);
    assert(path != nullptr);
    assert(*path != 0);
    assert(path[strlen(path) - 1] == '/');
    assert(suffix != nullptr);

    char *unescaped = uri_unescape_dup(pool, suffix, strlen(suffix));

    auto dest = NewFromPool<struct nfs_address>(*pool,
                                                p_strdup(pool, server),
                                                p_strdup(pool, export_name),
                                                p_strcat(pool, path,
                                                         unescaped, nullptr));
    dest->content_type = p_strdup_checked(pool, content_type);
    return dest;
}

const struct nfs_address *
nfs_address::Expand(struct pool *pool, const GMatchInfo *match_info,
                    GError **error_r) const
{
    assert(pool != nullptr);
    assert(match_info != nullptr);

    if (expand_path == nullptr)
        return this;

    const char *new_path = expand_string_unescaped(pool, expand_path,
                                                   match_info, error_r);
    if (new_path == nullptr)
        return nullptr;

    auto dest = NewFromPool<struct nfs_address>(*pool, server, export_name,
                                                new_path);
    dest->content_type = p_strdup_checked(pool, content_type);
    return dest;
}
