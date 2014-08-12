/*
 * OO representation of a struct sockaddr.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "SocketAddress.hxx"

#include <string.h>

bool
SocketAddress::operator==(SocketAddress other) const
{
    return size == other.size && memcmp(address, other.address, size) == 0;
}
