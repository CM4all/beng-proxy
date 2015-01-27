/*
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "file_address.hxx"
#include "uri_base.hxx"
#include "uri_escape.hxx"
#include "regex.hxx"
#include "pool.hxx"

#include <assert.h>
#include <string.h>

file_address::file_address(const char *_path)
    :path(_path),
     deflated(nullptr), gzipped(nullptr),
     content_type(nullptr),
     delegate(nullptr),
     document_root(nullptr),
     expand_path(nullptr),
     expand_document_root(nullptr),
     auto_gzipped(false)
{
    child_options.Init();
}

file_address::file_address(struct pool *pool, const file_address &src)
    :path(p_strdup(pool, src.path)),
     deflated(p_strdup_checked(pool, src.deflated)),
     gzipped(p_strdup_checked(pool, src.gzipped)),
     content_type(p_strdup_checked(pool, src.content_type)),
     delegate(p_strdup_checked(pool, src.delegate)),
     document_root(p_strdup_checked(pool, src.document_root)),
     expand_path(p_strdup_checked(pool, src.expand_path)),
     expand_document_root(p_strdup_checked(pool, src.expand_document_root)),
     auto_gzipped(src.auto_gzipped) {
    child_options.CopyFrom(pool, &src.child_options);
}

struct file_address *
file_address_new(struct pool &pool, const char *path)
{
    return NewFromPool<struct file_address>(pool, path);
}

struct file_address *
file_address_dup(struct pool &pool, const struct file_address *src)
{
    return NewFromPool<struct file_address>(pool, &pool, *src);
}

bool
file_address::IsValidBase() const
{
    return IsExpandable() || is_base(path);
}

struct file_address *
file_address::SaveBase(struct pool *pool, const char *suffix) const
{
    assert(pool != nullptr);
    assert(suffix != nullptr);

    size_t length = base_string_unescape(pool, path, suffix);
    if (length == (size_t)-1)
        return nullptr;

    struct file_address *dest = file_address_dup(*pool, this);
    dest->path = p_strndup(pool, dest->path, length);

    /* BASE+DEFLATED is not supported */
    dest->deflated = nullptr;
    dest->gzipped = nullptr;

    return dest;
}

struct file_address *
file_address::LoadBase(struct pool *pool, const char *suffix) const
{
    assert(pool != nullptr);
    assert(path != nullptr);
    assert(*path != 0);
    assert(path[strlen(path) - 1] == '/');
    assert(suffix != nullptr);

    char *unescaped = uri_unescape_dup(pool, suffix, strlen(suffix));

    struct file_address *dest = file_address_dup(*pool, this);
    dest->path = p_strcat(pool, dest->path, unescaped, nullptr);
    return dest;
}

bool
file_address::Expand(struct pool *pool, const GMatchInfo *match_info,
                     GError **error_r)
{
    assert(pool != nullptr);
    assert(match_info != nullptr);

    if (expand_path != nullptr) {
        path = expand_string_unescaped(pool, expand_path, match_info, error_r);
        if (path == nullptr)
            return false;
    }

    if (expand_document_root != nullptr) {
        document_root = expand_string_unescaped(pool, expand_document_root,
                                                match_info, error_r);
        if (document_root == nullptr)
            return false;
    }

    return true;
}
