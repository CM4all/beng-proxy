/*
 * Copyright 2007-2017 Content Management AG
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

#include "address_list.hxx"
#include "net/AddressInfo.hxx"
#include "net/ToString.hxx"
#include "AllocatorPtr.hxx"

#include <string.h>

AddressList::AddressList(ShallowCopy, const AddressInfoList &src) noexcept
{
    for (const auto &i : src) {
        if (addresses.full())
            break;

        addresses.push_back(i);
    }
}

AddressList::AddressList(AllocatorPtr alloc, const AddressList &src) noexcept
    :sticky_mode(src.sticky_mode)
{
    addresses.clear();

    for (const auto &i : src)
        Add(alloc, i);
}

bool
AddressList::Add(AllocatorPtr alloc, const SocketAddress address) noexcept
{
    if (addresses.full())
        return false;

    const struct sockaddr *new_address = (const struct sockaddr *)
        alloc.Dup(address.GetAddress(), address.GetSize());
    addresses.push_back({new_address, address.GetSize()});
    return true;
}

bool
AddressList::Add(AllocatorPtr alloc, const AddressInfoList &list) noexcept
{
    for (const auto &i : list)
        if (!Add(alloc, i))
            return false;

    return true;
}

const char *
AddressList::GetKey() const noexcept
{
    static char buffer[2048];
    size_t length = 0;
    bool success;

    for (const auto &i : *this) {
        if (length > 0 && length < sizeof(buffer) - 1)
            buffer[length++] = ' ';

        success = ToString(buffer + length, sizeof(buffer) - length, i);
        if (success)
            length += strlen(buffer + length);
    }

    buffer[length] = 0;

    return buffer;
}
