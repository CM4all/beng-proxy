// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <max.kellermann@ionos.com>

#pragma once

/**
 * The score of a connection.  This is used under high load to
 * estimate which connections should be dropped first, as a remedy for
 * denial of service attacks.
 */
enum class HttpServerScore {
	/**
	 * Connection has been accepted, but client hasn't sent any data
	 * yet.
	 */
	NEW,

	/**
	 * Client is transmitting the very first request.
	 */
	FIRST,

	/**
	 * At least one request was completed, but none was successful.
	 */
	ERROR,

	/**
	 * At least one request was completed successfully.
	 */
	SUCCESS,
};
