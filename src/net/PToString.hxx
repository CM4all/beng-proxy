// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <max.kellermann@ionos.com>

#pragma once

class AllocatorPtr;
class SocketAddress;

/**
 * Converts a sockaddr into a human-readable string in the form
 * "IP:PORT".
 */
[[gnu::pure]]
const char *
address_to_string(AllocatorPtr alloc, SocketAddress address);

/**
 * Converts a sockaddr into a human-readable string containing the
 * numeric IP address, ignoring the port number.
 */
[[gnu::pure]]
const char *
address_to_host_string(AllocatorPtr alloc, SocketAddress address);
