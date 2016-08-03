/*
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef NET_INTERFACE_HXX
#define NET_INTERFACE_HXX

#include <inline/compiler.h>

class SocketAddress;

/**
 * Find a network interface with the given address.
 *
 * @return the interface index or 0 if no matching network interface
 * was found
 */
gcc_pure
unsigned
FindNetworkInterface(SocketAddress address);

#endif
