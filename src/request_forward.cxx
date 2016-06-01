/*
 * Common request forwarding code for the request handlers.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "request_forward.hxx"
#include "request.hxx"
#include "http_server/Request.hxx"
#include "header_forward.hxx"

void
request_forward(struct forward_request &dest, Request &request2,
                const struct header_forward_settings &header_forward,
                const char *host_and_port, const char *uri,
                bool exclude_host)
{
    const auto &request = request2.request;

    assert(!request.HasBody() || request2.body != nullptr);

    /* send a request body? */

    if (request2.processor_focus) {
        /* reserve method+body for the processor, and
           convert this request to a GET */

        dest.method = HTTP_METHOD_GET;
        dest.body = nullptr;
    } else {
        /* forward body (if any) to the real server */

        dest.method = request.method;
        dest.body = request2.body;
        request2.body = nullptr;
    }

    /* generate request headers */

    auto session = request2.GetRealmSession();
    dest.headers = forward_request_headers(request2.pool, request.headers,
                                           request.local_host_and_port,
                                           request.remote_host,
                                           exclude_host,
                                           dest.body != nullptr,
                                           !request2.IsProcessorEnabled(),
                                           !request2.IsTransformationEnabled(),
                                           !request2.IsTransformationEnabled(),
                                           header_forward,
                                           request2.session_cookie,
                                           session.get(), host_and_port, uri);
}
