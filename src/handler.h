/*
 * Handler for HTTP requests.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef __BENG_HANDLER_H
#define __BENG_HANDLER_H

struct request;

extern const struct http_server_connection_handler my_http_server_connection_handler;

void
file_callback(struct request *request);

void
cgi_handler(struct request *request2);

void
proxy_handler(struct request *request);

#endif
