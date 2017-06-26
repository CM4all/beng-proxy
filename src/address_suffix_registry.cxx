/*
 * Interface for Content-Types managed by the translation server.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "address_suffix_registry.hxx"
#include "suffix_registry.hxx"
#include "ResourceAddress.hxx"
#include "file_address.hxx"
#include "nfs/Address.hxx"
#include "pool.hxx"
#include "util/ConstBuffer.hxx"
#include "util/CharUtil.hxx"

#include <string.h>

gcc_pure
static const char *
get_suffix(const char *path)
{
    const char *slash = strrchr(path, '/');
    if (slash != nullptr)
        path = slash + 1;

    while (*path == '.')
        ++path;

    const char *dot = strrchr(path, '.');
    if (dot == nullptr || dot[1] == 0)
        return nullptr;

    return dot + 1;
}

struct AddressSuffixInfo {
    const char *path;
    ConstBuffer<void> content_type_lookup;
};

gcc_pure
static AddressSuffixInfo
GetAddressSuffixInfo(const ResourceAddress &address)
{
    switch (address.type) {
    case ResourceAddress::Type::NONE:
    case ResourceAddress::Type::HTTP:
    case ResourceAddress::Type::LHTTP:
    case ResourceAddress::Type::PIPE:
    case ResourceAddress::Type::CGI:
    case ResourceAddress::Type::FASTCGI:
    case ResourceAddress::Type::WAS:
        return {nullptr, nullptr};

    case ResourceAddress::Type::LOCAL:
        return {address.GetFile().path, address.GetFile().content_type_lookup};

    case ResourceAddress::Type::NFS:
        return {address.GetNfs().path, address.GetNfs().content_type_lookup};
    }

    gcc_unreachable();
}

bool
suffix_registry_lookup(struct pool &pool, struct tcache &translate_cache,
                       const ResourceAddress &address,
                       const SuffixRegistryHandler &handler, void *ctx,
                       CancellablePointer &cancel_ptr)
{
    const auto info = GetAddressSuffixInfo(address);
    if (info.content_type_lookup.IsNull())
            return false;

    const char *suffix = get_suffix(info.path);
    if (suffix == nullptr)
        return false;

    const size_t length = strlen(suffix);
    if (length > 5)
        return false;

    /* duplicate the suffix, convert to lower case, check for
       "illegal" characters (non-alphanumeric) */
    char *buffer = p_strdup(&pool, suffix);
    for (char *p = buffer; *p != 0; ++p) {
        const char ch = *p;
        if (IsUpperAlphaASCII(ch))
            /* convert to lower case */
            *p += 'a' - 'A';
        else if (!IsLowerAlphaASCII(ch) && !IsDigitASCII(ch))
            /* no, we won't look this up */
            return false;
    }

    suffix_registry_lookup(pool, translate_cache,
                           info.content_type_lookup, buffer,
                           handler, ctx, cancel_ptr);
    return true;
}
