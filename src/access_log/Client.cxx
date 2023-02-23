// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#include "Client.hxx"
#include "net/log/Send.hxx"

using namespace Net::Log;

bool
LogClient::Send(const Datagram &d) noexcept
{
	try {
		Net::Log::Send(fd, d);
		return true;
	} catch (...) {
		logger(1, std::current_exception());
		return false;
	}
}
