/*
 * Interface for Content-Types managed by the translation server.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "address_suffix_registry.hxx"
#include "suffix_registry.hxx"
#include "resource_address.hxx"
#include "file_address.hxx"
#include "nfs_address.hxx"
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

bool
suffix_registry_lookup(struct pool &pool, struct tcache &translate_cache,
                       const struct resource_address &address,
                       const SuffixRegistryHandler &handler, void *ctx,
                       struct async_operation_ref &async_ref)
{
    ConstBuffer<void> content_type_lookup;
    const char *path;

    switch (address.type) {
    case RESOURCE_ADDRESS_NONE:
    case RESOURCE_ADDRESS_HTTP:
    case RESOURCE_ADDRESS_LHTTP:
    case RESOURCE_ADDRESS_AJP:
    case RESOURCE_ADDRESS_PIPE:
    case RESOURCE_ADDRESS_CGI:
    case RESOURCE_ADDRESS_FASTCGI:
    case RESOURCE_ADDRESS_WAS:
        return false;

    case RESOURCE_ADDRESS_LOCAL:
        content_type_lookup = address.u.file->content_type_lookup;
        path = address.u.file->path;
        break;

    case RESOURCE_ADDRESS_NFS:
        content_type_lookup = address.u.nfs->content_type_lookup;
        path = address.u.nfs->path;
        break;
    }

    if (content_type_lookup.IsNull())
            return false;

    const char *suffix = get_suffix(path);
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
                           content_type_lookup, buffer,
                           handler, ctx, async_ref);
    return true;
}
