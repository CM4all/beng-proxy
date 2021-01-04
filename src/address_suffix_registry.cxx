/*
 * Copyright 2007-2021 CM4all GmbH
 * All rights reserved.
 *
 * author: Max Kellermann <mk@cm4all.com>
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * - Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 *
 * - Redistributions in binary form must reproduce the above copyright
 * notice, this list of conditions and the following disclaimer in the
 * documentation and/or other materials provided with the
 * distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE
 * FOUNDATION OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED
 * OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "address_suffix_registry.hxx"
#include "suffix_registry.hxx"
#include "ResourceAddress.hxx"
#include "file_address.hxx"
#include "nfs/Address.hxx"
#include "pool/pool.hxx"
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
suffix_registry_lookup(struct pool &pool, TranslationService &service,
                       const ResourceAddress &address,
                       const StopwatchPtr &parent_stopwatch,
                       SuffixRegistryHandler &handler,
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

    suffix_registry_lookup(pool, service,
                           info.content_type_lookup, buffer,
                           parent_stopwatch,
                           handler, cancel_ptr);
    return true;
}
