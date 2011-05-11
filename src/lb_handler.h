/*
 * Handler for HTTP requests.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_LB_HANDLER_H
#define BENG_PROXY_LB_HANDLER_H

struct lb_connection;
struct http_server_request;
struct async_operation_ref;

void
handle_http_request(struct lb_connection *connection,
                    struct http_server_request *request,
                    struct async_operation_ref *async_ref);

#endif
