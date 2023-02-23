// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#ifndef BENG_ADDRESS_STRING_HXX
#define BENG_ADDRESS_STRING_HXX

struct pool;
class SocketAddress;

/**
 * Converts a sockaddr into a human-readable string in the form
 * "IP:PORT".
 */
[[gnu::pure]]
const char *
address_to_string(struct pool &pool, SocketAddress address);

/**
 * Converts a sockaddr into a human-readable string containing the
 * numeric IP address, ignoring the port number.
 */
[[gnu::pure]]
const char *
address_to_host_string(struct pool &pool, SocketAddress address);

#endif
