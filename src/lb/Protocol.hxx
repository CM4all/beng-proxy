// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#pragma once

enum class LbProtocol {
	HTTP,
	TCP,
};

/**
 * Does the given protocol require a port number?
 */
constexpr bool
NeedsPort(LbProtocol protocol) noexcept
{
	/* all currently implemented protocols need a port */
	switch (protocol) {
	case LbProtocol::HTTP:
	case LbProtocol::TCP:
		return true;
	}

	return false;
}
