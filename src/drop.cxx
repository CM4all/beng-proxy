/*
 * Copyright 2007-2017 Content Management AG
 * All rights reserved.
 *
 * author: Max Kellermann <mk@cm4all.com>
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * - Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 *
 * - Redistributions in binary form must reproduce the above copyright
 * notice, this list of conditions and the following disclaimer in the
 * documentation and/or other materials provided with the
 * distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE
 * FOUNDATION OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED
 * OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "drop.hxx"
#include "bp_connection.hxx"
#include "bp_instance.hxx"
#include "http_server/http_server.hxx"
#include "io/Logger.hxx"
#include "util/Macros.hxx"

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

    LogConcat(2, "drop", "dropping ", num_connections, " out of ",
              (unsigned)instance->connections.size(), "connections");

    for (unsigned i = 0; i < num_connections; ++i)
        close_connection(connections[i]);

    return num_connections;
}
