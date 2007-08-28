/*
 * Handler for HTTP requests.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef __BENG_HANDLER_H
#define __BENG_HANDLER_H

struct translated {
    const char *path;
    const char *path_info;
};

extern const struct http_server_connection_handler my_http_server_connection_handler;

void
file_callback(struct client_connection *connection,
              struct http_server_request *request,
              struct translated *translated);

void
proxy_callback(struct client_connection *connection,
               struct http_server_request *request,
               struct translated *translated);

#endif
