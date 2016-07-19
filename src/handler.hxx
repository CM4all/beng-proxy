/*
 * Handler for HTTP requests.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_HANDLER_HXX
#define BENG_PROXY_HANDLER_HXX

struct Request;
struct BpConnection;
struct HttpServerRequest;
struct DelegateAddress;
class CancellablePointer;

void
delegate_handler(Request &request, const DelegateAddress &address,
                 const char *path);

void
cgi_handler(Request &request2);

void
fcgi_handler(Request &request2);

void
proxy_handler(Request &request);

void
handle_http_request(BpConnection &connection,
                    HttpServerRequest &request,
                    CancellablePointer &cancel_ptr);

#endif
