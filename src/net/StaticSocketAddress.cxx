/*
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "StaticSocketAddress.hxx"

#include <assert.h>
#include <string.h>

StaticSocketAddress &
StaticSocketAddress::operator=(const SocketAddress &src)
{
    assert(!src.IsNull());
    assert(src.GetSize() <= sizeof(address));

    size = src.GetSize();
    memcpy(&address, src.GetAddress(), size);

    return *this;
}
