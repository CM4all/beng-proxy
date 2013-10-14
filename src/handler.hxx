/*
 * Handler for HTTP requests.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_HANDLER_HXX
#define BENG_PROXY_HANDLER_HXX

struct request;
struct client_connection;
struct http_server_request;
struct async_operation_ref;

void
delegate_handler(request &request);

void
cgi_handler(request &request2);

void
fcgi_handler(request &request2);

void
proxy_handler(request &request);

void
handle_http_request(client_connection &connection,
                    http_server_request &request,
                    struct async_operation_ref *async_ref);

#endif
