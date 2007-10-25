/*
 * Handler for HTTP requests.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef __BENG_HANDLER_H
#define __BENG_HANDLER_H

#include "uri.h"

struct translated {
    struct parsed_uri uri;
    const char *path;
};

extern const struct http_server_connection_handler my_http_server_connection_handler;

void
file_callback(struct http_server_request *request,
              struct translated *translated);

void
proxy_callback(struct client_connection *connection,
               struct http_server_request *request,
               struct translated *translated);

#endif
