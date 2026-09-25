// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <max.kellermann@ionos.com>

#include "Listener.hxx"
#include "Connection.hxx"
#include "http/server/Public.hxx"
#include "io/Logger.hxx"

#include <limits>

std::size_t
BpListener::DropSomeConnections() noexcept
{
	if (const auto n = listener.DropSomePendingConnections())
		/* connections which have not yet finished the TLS
		   handshake have been dropped */
		return n;

	BpConnection *candidates[32];
	std::size_t num_candidates = 0;
	http_server_score min_score = std::numeric_limits<http_server_score>::max();

	/* collect a list of the lowest-score connections */

	for (auto &c : connections) {
		/* HTTP/2 connections have no HttpServerConnection and
		   thus no score; assume they are in use */
		const enum http_server_score score = c.http != nullptr
			? http_server_connection_score(c.http)
			/* TODO score not implemented on HTTP/2 */
			: HTTP_SERVER_SUCCESS;

		if (score < min_score) {
			/* found a new minimum - clear the old list */

			num_candidates = 0;
			min_score = score;
		}

		if (score == min_score &&
		    num_candidates < std::size(candidates)) {
			candidates[num_candidates++] = &c;

			if (score == HTTP_SERVER_NEW &&
			    num_candidates >= std::size(candidates))
				break;
		}
	}

	/* now close the connections we have selected */

	LogConcat(2, "drop", "dropping ", num_candidates, " out of ",
		  (unsigned)connections.size(), "connections");

	for (std::size_t i = 0; i < num_candidates; ++i)
		CloseConnection(*candidates[i]);

	return num_candidates;
}
