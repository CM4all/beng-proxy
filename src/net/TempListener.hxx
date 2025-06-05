// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <max.kellermann@ionos.com>

#pragma once

#include "net/SocketAddress.hxx"

#include <sys/un.h>

class UniqueSocketDescriptor;

/**
 * Create a listener socket on a temporary socket file.  The file will
 * be deleted automatically by the destructor.
 */
class TempListener {
	struct sockaddr_un address;

public:
	TempListener() noexcept {
		address.sun_family = AF_UNSPEC;
	}

	~TempListener() noexcept;

	constexpr TempListener(TempListener &&src) noexcept
		:address(src.address)
	{
		src.address.sun_family = AF_UNSPEC;
	}

	TempListener &operator=(const TempListener &) = delete;

	bool IsDefined() const noexcept {
		return GetAddress().IsDefined();
	}

	const char *GetPath() const noexcept {
		return address.sun_path;
	}

	/**
	 * Throws std::runtime_error on error.
	 */
	UniqueSocketDescriptor Create(int socket_type, int backlog);

	SocketAddress GetAddress() const noexcept {
		return SocketAddress((const struct sockaddr *)&address,
				     SUN_LEN(&address));
	}

	/**
	 * Throws std::runtime_error on error.
	 */
	UniqueSocketDescriptor Connect() const;
};
