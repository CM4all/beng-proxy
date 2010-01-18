/*
 * Dropping client connections.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_DROP_H
#define BENG_PROXY_DROP_H

struct instance;

/**
 * Drop client connections, starting with the lowest score (see
 * http_server_connection_score()).  This is used to relieve some of
 * the load on an overloaded machine (e.g. when the number of
 * connections exceeds the configured limit).
 *
 * @return the number of connections which were dropped
 */
unsigned
drop_some_connections(struct instance *instance);

#endif
