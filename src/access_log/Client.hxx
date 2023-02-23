// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#pragma once

#include "net/UniqueSocketDescriptor.hxx"
#include "io/Logger.hxx"

namespace Net { namespace Log { struct Datagram; }}

/**
 * A client for the logging protocol.
 */
class LogClient {
	const LLogger logger;

	UniqueSocketDescriptor fd;

public:
	explicit LogClient(UniqueSocketDescriptor &&_fd) noexcept
		:logger("access_log"), fd(std::move(_fd)) {}

	SocketDescriptor GetSocket() noexcept {
		return fd;
	}

	bool Send(const Net::Log::Datagram &d) noexcept;
};
