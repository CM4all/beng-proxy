/*
 * Dropping client connections.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "drop.hxx"
#include "bp_connection.hxx"
#include "bp_instance.hxx"
#include "http_server/http_server.hxx"
#include "util/Macros.hxx"

#include <daemon/log.h>

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
            num_connections < ARRAY_SIZE(connections)) {
            connections[num_connections++] = &c;

            if (score == HTTP_SERVER_NEW &&
                num_connections >= ARRAY_SIZE(connections))
                break;
        }
    }

    /* now close the connections we have selected */

    daemon_log(2, "dropping %u out of %zu connections\n",
               num_connections, instance->connections.size());

    for (unsigned i = 0; i < num_connections; ++i)
        close_connection(connections[i]);

    return num_connections;
}
