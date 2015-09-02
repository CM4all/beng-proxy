/*
 * Handler for HTTP requests.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_HANDLER_HXX
#define BENG_PROXY_HANDLER_HXX

struct Request;
struct client_connection;
struct http_server_request;
struct async_operation_ref;

void
delegate_handler(Request &request);

void
cgi_handler(Request &request2);

void
fcgi_handler(Request &request2);

void
proxy_handler(Request &request);

void
handle_http_request(client_connection &connection,
                    http_server_request &request,
                    struct async_operation_ref &async_ref);

#endif
