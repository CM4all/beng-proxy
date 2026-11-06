// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <max.kellermann@ionos.com>

#include "Listener.hxx"
#include "Connection.hxx"
#include "http/server/Public.hxx"
#include "io/Logger.hxx"

#ifdef HAVE_NGHTTP2
#include "nghttp2/Server.hxx"
#endif

#include <limits>
#include <utility> // for std::unreachable()

[[gnu::pure]]
static HttpServerScore
GetScore(const BpConnection &c) noexcept
{
	if (c.http != nullptr)
		return http_server_connection_score(c.http);

#ifdef HAVE_NGHTTP2
	return c.http2->GetScore();
#else
	std::unreachable();
#endif
}

std::size_t
BpListener::DropSomeConnections() noexcept
{
	if (const auto n = listener.DropSomePendingConnections())
		/* connections which have not yet finished the TLS
		   handshake have been dropped */
		return n;

	BpConnection *candidates[32];
	std::size_t num_candidates = 0;
	HttpServerScore min_score = std::numeric_limits<HttpServerScore>::max();

	/* collect a list of the lowest-score connections */

	for (auto &c : connections) {
		const HttpServerScore score = GetScore(c);

		if (score < min_score) {
			/* found a new minimum - clear the old list */

			num_candidates = 0;
			min_score = score;
		}

		if (score == min_score &&
		    num_candidates < std::size(candidates)) {
			candidates[num_candidates++] = &c;

			if (score == HttpServerScore::NEW &&
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
