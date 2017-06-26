/*
 * A simple server for the logging protocol.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_LOG_SERVER_H
#define BENG_PROXY_LOG_SERVER_H

struct log_datagram;

struct log_server *
log_server_new(int fd);

void
log_server_free(struct log_server *server);

const struct log_datagram *
log_server_receive(struct log_server *server);

#endif
