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
#include "pbuffer.hxx"
#include "AllocatorPtr.hxx"

#include <assert.h>
#include <string.h>

FileAddress::FileAddress(AllocatorPtr alloc, const FileAddress &src)
    :path(alloc.Dup(src.path)),
     deflated(alloc.CheckDup(src.deflated)),
     gzipped(alloc.CheckDup(src.gzipped)),
     content_type(alloc.CheckDup(src.content_type)),
     content_type_lookup(alloc.Dup(src.content_type_lookup)),
     document_root(alloc.CheckDup(src.document_root)),
     expand_path(alloc.CheckDup(src.expand_path)),
     expand_document_root(alloc.CheckDup(src.expand_document_root)),
     delegate(src.delegate != nullptr
              ? alloc.New<DelegateAddress>(alloc, *src.delegate)
              : nullptr),
     auto_gzipped(src.auto_gzipped) {
}

void
FileAddress::Check() const
{
    if (delegate != nullptr)
        delegate->Check();
}

bool
FileAddress::IsValidBase() const
{
    return IsExpandable() || is_base(path);
}

FileAddress *
FileAddress::SaveBase(AllocatorPtr alloc, const char *suffix) const
{
    assert(suffix != nullptr);

    size_t length = base_string_unescape(alloc, path, suffix);
    if (length == (size_t)-1)
        return nullptr;

    auto *dest = alloc.New<FileAddress>(alloc, *this);
    dest->path = alloc.DupZ({dest->path, length});

    /* BASE+DEFLATED is not supported */
    dest->deflated = nullptr;
    dest->gzipped = nullptr;

    return dest;
}

FileAddress *
FileAddress::LoadBase(AllocatorPtr alloc, const char *suffix) const
{
    assert(path != nullptr);
    assert(*path != 0);
    assert(path[strlen(path) - 1] == '/');
    assert(suffix != nullptr);

    char *unescaped = uri_unescape_dup(alloc, suffix);
    if (unescaped == nullptr)
        return nullptr;

    auto *dest = alloc.New<FileAddress>(alloc, *this);
    dest->path = alloc.Concat(dest->path, unescaped);
    return dest;
}

bool
FileAddress::IsExpandable() const
{
    return expand_path != nullptr ||
        expand_document_root != nullptr ||
        (delegate != nullptr && delegate->IsExpandable());
}

void
FileAddress::Expand(struct pool *pool, const MatchInfo &match_info)
{
    assert(pool != nullptr);

    if (expand_path != nullptr)
        path = expand_string_unescaped(pool, expand_path, match_info);

    if (expand_document_root != nullptr)
        document_root = expand_string_unescaped(pool, expand_document_root,
                                                match_info);

    if (delegate != nullptr)
        delegate->Expand(*pool, match_info);
}
