// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#pragma once

/**
 * Return type for IstreamHandler::OnIstreamReady().
 */
enum class IstreamReadyResult {
	/**
	 * The callee acknowledges the readiness and has finished
	 * processing data.  It might or might not have consumed data
	 * from the #Istream.
	 */
	OK,

	/**
	 * The #Istream shall now invoke IstreamHandler::OnData() or
	 * IstreamHandler::OnDirect().
	 */
	FALLBACK,

	/**
	 * The calling #Istream has been closed.
	 */
	CLOSED,
};

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
	 * The handler is using this file descriptor in an
	 * asynchronous operation.  When finished,
	 * Istream::ConsumeDirect() will be called.  Since the handler
	 * has a pending opertion, the caller does not need to
	 * schedule reading.
	 */
	ASYNC,

	/**
	 * The stream has ben closed.  This state supersedes all other
	 * states.
	 */
	CLOSED,
};
