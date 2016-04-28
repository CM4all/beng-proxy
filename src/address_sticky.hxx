/*
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_ADDRESS_STICKY_HXX
#define BENG_PROXY_ADDRESS_STICKY_HXX

#include <inline/compiler.h>

class SocketAddress;

gcc_pure
unsigned
socket_address_sticky(SocketAddress address);

#endif
