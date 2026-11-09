// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <max.kellermann@ionos.com>

#pragma once

/**
 * The score of a connection.  This is used under high load to
 * estimate which connections should be dropped first, as a remedy for
 * denial of service attacks.
 */
enum http_server_score {
	/**
	 * Connection has been accepted, but client hasn't sent any data
	 * yet.
	 */
	HTTP_SERVER_NEW,

	/**
	 * Client is transmitting the very first request.
	 */
	HTTP_SERVER_FIRST,

	/**
	 * At least one request was completed, but none was successful.
	 */
	HTTP_SERVER_ERROR,

	/**
	 * At least one request was completed successfully.
	 */
	HTTP_SERVER_SUCCESS,
};
