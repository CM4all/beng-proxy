/*
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "AllocatorPtr.hxx"
#include "util/StringView.hxx"

StringView
AllocatorPtr::Dup(StringView src)
{
    if (src.IsNull())
        return nullptr;

    if (src.IsEmpty())
        return "";

    return {(const char *)Dup(src.data, src.size), src.size};
}

const char *
AllocatorPtr::DupZ(StringView src)
{
    if (src.IsNull())
        return nullptr;

    if (src.IsEmpty())
        return "";

    return p_strndup(&pool, src.data, src.size);
}

