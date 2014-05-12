/*
 * Dropping client connections.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "drop.hxx"
#include "bp_connection.hxx"
#include "bp_instance.hxx"
#include "http_server.hxx"

#include <glib.h>

#include <daemon/log.h>

#include <assert.h>

unsigned
drop_some_connections(struct instance *instance)
{
    struct client_connection *connections[32];
    unsigned num_connections = 0;
    http_server_score min_score = (http_server_score)G_MAXSHORT;

    assert(instance != NULL);

    /* collect a list of the lowest-score connections */

    for (struct client_connection *c = (struct client_connection *)instance->connections.next;
         &c->siblings != &instance->connections;
         c = (struct client_connection *)c->siblings.next) {
        enum http_server_score score = http_server_connection_score(c->http);

        if (score < min_score) {
            /* found a new minimum - clear the old list */

            num_connections = 0;
            min_score = score;
        }

        if (score == min_score &&
            num_connections < G_N_ELEMENTS(connections)) {
            connections[num_connections++] = c;

            if (score == HTTP_SERVER_NEW &&
                num_connections >= G_N_ELEMENTS(connections))
                break;
        }
    }

    /* now close the connections we have selected */

    daemon_log(2, "dropping %u out of %u connections\n",
               num_connections, instance->num_connections);

    for (unsigned i = 0; i < num_connections; ++i)
        close_connection(connections[i]);

    return num_connections;
}
