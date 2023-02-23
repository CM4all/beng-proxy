// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#include "drop.hxx"
#include "Connection.hxx"
#include "Instance.hxx"
#include "http/server/Public.hxx"
#include "io/Logger.hxx"

#include <limits>

#include <assert.h>

unsigned
drop_some_connections(BpInstance *instance)
{
	BpConnection *connections[32];
	unsigned num_connections = 0;
	http_server_score min_score = std::numeric_limits<http_server_score>::max();

	assert(instance != NULL);

	/* collect a list of the lowest-score connections */

	for (auto &c : instance->connections) {
		enum http_server_score score = http_server_connection_score(c.http);

		if (score < min_score) {
			/* found a new minimum - clear the old list */

			num_connections = 0;
			min_score = score;
		}

		if (score == min_score &&
		    num_connections < std::size(connections)) {
			connections[num_connections++] = &c;

			if (score == HTTP_SERVER_NEW &&
			    num_connections >= std::size(connections))
				break;
		}
	}

	/* now close the connections we have selected */

	LogConcat(2, "drop", "dropping ", num_connections, " out of ",
		  (unsigned)instance->connections.size(), "connections");

	for (unsigned i = 0; i < num_connections; ++i)
		close_connection(connections[i]);

	return num_connections;
}
