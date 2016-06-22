/*
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "file_address.hxx"
#include "delegate/Address.hxx"
#include "uri/uri_base.hxx"
#include "util/StringView.hxx"
#include "puri_base.hxx"
#include "puri_escape.hxx"
#include "pexpand.hxx"
#include "pool.hxx"
#include "pbuffer.hxx"

#include <assert.h>
#include <string.h>

FileAddress::FileAddress(struct pool *pool, const FileAddress &src)
    :path(p_strdup(pool, src.path)),
     deflated(p_strdup_checked(pool, src.deflated)),
     gzipped(p_strdup_checked(pool, src.gzipped)),
     content_type(p_strdup_checked(pool, src.content_type)),
     content_type_lookup(DupBuffer(*pool, src.content_type_lookup)),
     document_root(p_strdup_checked(pool, src.document_root)),
     expand_path(p_strdup_checked(pool, src.expand_path)),
     expand_document_root(p_strdup_checked(pool, src.expand_document_root)),
     delegate(src.delegate != nullptr
              ? NewFromPool<DelegateAddress>(*pool, *src.delegate)
              : nullptr),
     auto_gzipped(src.auto_gzipped) {
}

bool
FileAddress::Check(GError **error_r) const
{
    return delegate == nullptr || delegate->Check(error_r);
}

FileAddress *
file_address_new(struct pool &pool, const char *path)
{
    return NewFromPool<FileAddress>(pool, path);
}

FileAddress *
file_address_dup(struct pool &pool, const FileAddress *src)
{
    return NewFromPool<FileAddress>(pool, &pool, *src);
}

bool
FileAddress::IsValidBase() const
{
    return IsExpandable() || is_base(path);
}

FileAddress *
FileAddress::SaveBase(struct pool *pool, const char *suffix) const
{
    assert(pool != nullptr);
    assert(suffix != nullptr);

    size_t length = base_string_unescape(pool, path, suffix);
    if (length == (size_t)-1)
        return nullptr;

    FileAddress *dest = file_address_dup(*pool, this);
    dest->path = p_strndup(pool, dest->path, length);

    /* BASE+DEFLATED is not supported */
    dest->deflated = nullptr;
    dest->gzipped = nullptr;

    return dest;
}

FileAddress *
FileAddress::LoadBase(struct pool *pool, const char *suffix) const
{
    assert(pool != nullptr);
    assert(path != nullptr);
    assert(*path != 0);
    assert(path[strlen(path) - 1] == '/');
    assert(suffix != nullptr);

    char *unescaped = uri_unescape_dup(pool, suffix);
    if (unescaped == nullptr)
        return nullptr;

    FileAddress *dest = file_address_dup(*pool, this);
    dest->path = p_strcat(pool, dest->path, unescaped, nullptr);
    return dest;
}

bool
FileAddress::IsExpandable() const
{
    return expand_path != nullptr ||
        expand_document_root != nullptr ||
        (delegate != nullptr && delegate->IsExpandable());
}

bool
FileAddress::Expand(struct pool *pool, const MatchInfo &match_info,
                    Error &error_r)
{
    assert(pool != nullptr);

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

    if (delegate != nullptr && !delegate->Expand(*pool, match_info, error_r))
        return false;

    return true;
}
