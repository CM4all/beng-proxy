/*
 * Handler for raw TCP connections.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_LB_TCP_H
#define BENG_PROXY_LB_TCP_H

#include "istream-direct.h"

struct sockaddr;
struct lb_connection;

void
lb_tcp_new(struct lb_connection *connection, int fd,
           enum istream_direct fd_type,
           const struct sockaddr *remote_address);

void
lb_tcp_close(struct lb_connection *connection);

#endif
