/*
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_ADDRESS_STICKY_HXX
#define BENG_PROXY_ADDRESS_STICKY_HXX

#include "StickyHash.hxx"

#include "util/Compiler.h"

class SocketAddress;

gcc_pure
sticky_hash_t
socket_address_sticky(SocketAddress address);

#endif
