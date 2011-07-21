/*
 * Handler for raw TCP connections.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_LB_TCP_H
#define BENG_PROXY_LB_TCP_H

struct lb_connection;

void
lb_tcp_new(struct lb_connection *connection, int fd);

void
lb_tcp_close(struct lb_connection *connection);

#endif
