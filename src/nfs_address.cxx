/*
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "nfs_address.hxx"
#include "uri/uri_base.hxx"
#include "puri_base.hxx"
#include "puri_escape.hxx"
#include "pexpand.hxx"
#include "pool.hxx"
#include "pbuffer.hxx"
#include "AllocatorPtr.hxx"
#include "util/StringView.hxx"

#include <stdexcept>

#include <assert.h>
#include <string.h>

NfsAddress::NfsAddress(struct pool *pool, const NfsAddress &other)
    :server(p_strdup(pool, other.server)),
     export_name(p_strdup(pool, other.export_name)),
     path(p_strdup(pool, other.path)),
     expand_path(p_strdup_checked(pool, other.expand_path)),
     content_type(p_strdup_checked(pool, other.content_type)),
     content_type_lookup(DupBuffer(*pool, other.content_type_lookup)) {}

const char *
NfsAddress::GetId(struct pool *pool) const
{
    assert(server != nullptr);
    assert(export_name != nullptr);
    assert(path != nullptr);

    return p_strcat(pool, server, ":", export_name, ":", path, nullptr);
}

void
NfsAddress::Check() const
{
    if (export_name == nullptr || *export_name == 0)
        throw std::runtime_error("missing NFS_EXPORT");

    if (path == nullptr || *path == 0)
        throw std::runtime_error("missing NFS PATH");
}

bool
NfsAddress::IsValidBase() const
{
    return IsExpandable() || is_base(path);
}

NfsAddress *
NfsAddress::SaveBase(struct pool *pool, const char *suffix) const
{
    assert(pool != nullptr);
    assert(suffix != nullptr);

    size_t length = base_string_unescape(*pool, path, suffix);
    if (length == (size_t)-1)
        return nullptr;

    auto dest = NewFromPool<NfsAddress>(*pool,
                                        p_strdup(pool, server),
                                        p_strdup(pool, export_name),
                                        p_strndup(pool, path, length));
    dest->content_type = p_strdup_checked(pool, content_type);
    return dest;
}

NfsAddress *
NfsAddress::LoadBase(struct pool *pool, const char *suffix) const
{
    assert(pool != nullptr);
    assert(path != nullptr);
    assert(*path != 0);
    assert(path[strlen(path) - 1] == '/');
    assert(suffix != nullptr);

    char *unescaped = uri_unescape_dup(*pool, suffix);
    if (unescaped == nullptr)
        return nullptr;

    auto dest = NewFromPool<NfsAddress>(*pool,
                                        p_strdup(pool, server),
                                        p_strdup(pool, export_name),
                                        p_strcat(pool, path,
                                                 unescaped, nullptr));
    dest->content_type = p_strdup_checked(pool, content_type);
    return dest;
}

const NfsAddress *
NfsAddress::Expand(struct pool *pool, const MatchInfo &match_info) const
{
    assert(pool != nullptr);

    if (expand_path == nullptr)
        return this;

    const char *new_path = expand_string_unescaped(pool, expand_path,
                                                   match_info);

    auto dest = NewFromPool<NfsAddress>(*pool, server, export_name,
                                        new_path);
    dest->content_type = p_strdup_checked(pool, content_type);
    return dest;
}
