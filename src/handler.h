/*
 * Handler for HTTP requests.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef __BENG_HANDLER_H
#define __BENG_HANDLER_H

struct request;
struct client_connection;
struct http_server_request;
struct async_operation_ref;

void
cgi_handler(struct request *request2);

void
proxy_handler(struct request *request);

void
ajp_handler(struct request *request);

void
handle_http_request(struct client_connection *connection,
                    struct http_server_request *request,
                    struct async_operation_ref *async_ref);

#endif
