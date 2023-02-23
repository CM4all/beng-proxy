// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#pragma once

/**
 * These special values may be returned from
 * IstreamHandler::OnDirect().
 */
enum class IstreamDirectResult {
	/**
	 * Some data has been read.
	 */
	OK,

	/**
	 * No more data available in the specified socket.
	 */
	END,

	/**
	 * I/O error, errno set.
	 */
	ERRNO,

	/**
	 * Writing would block, callee is responsible for registering an
	 * event and calling Istream::Read().
	 */
	BLOCKING,

	/**
	 * The stream has ben closed.  This state supersedes all other
	 * states.
	 */
	CLOSED,
};
