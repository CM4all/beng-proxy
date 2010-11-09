/*
 * A simple server for the logging protocol.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_LOG_SERVER_H
#define BENG_PROXY_LOG_SERVER_H

#include <http/method.h>
#include <http/status.h>

#include <stdint.h>
#include <stdbool.h>

struct log_datagram {
    uint64_t timestamp;

    const char *remote_host, *site;

    http_method_t http_method;

    const char *http_uri, *http_referer, *user_agent;

    http_status_t http_status;

    uint64_t length;

    uint64_t traffic_received, traffic_sent;

    bool valid_timestamp, valid_http_method, valid_http_status;
    bool valid_length, valid_traffic;
};

struct log_server *
log_server_new(int fd);

void
log_server_free(struct log_server *server);

const struct log_datagram *
log_server_receive(struct log_server *server);

#endif
