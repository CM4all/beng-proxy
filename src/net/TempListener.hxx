/*
 * Copyright 2007-2020 CM4all GmbH
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
