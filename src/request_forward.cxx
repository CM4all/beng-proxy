/*
 * Common request forwarding code for the request handlers.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "request_forward.hxx"
#include "request.hxx"
#include "http_server/Request.hxx"
#include "header_forward.hxx"

ForwardRequest
request_forward(Request &request2,
                const struct header_forward_settings &header_forward,
                const char *host_and_port, const char *uri,
                bool exclude_host)
{
    const auto &request = request2.request;

    assert(!request.HasBody() || request2.body != nullptr);

    http_method_t method;
    Istream *body;

    /* send a request body? */

    if (request2.processor_focus) {
        /* reserve method+body for the processor, and
           convert this request to a GET */

        method = HTTP_METHOD_GET;
        body = nullptr;
    } else {
        /* forward body (if any) to the real server */

        method = request.method;
        body = request2.body;
        request2.body = nullptr;
    }

    /* generate request headers */

    auto *headers = forward_request_headers(request2.pool, request.headers,
                                            request.local_host_and_port,
                                            request.remote_host,
                                            exclude_host,
                                            body != nullptr,
                                            !request2.IsProcessorEnabled(),
                                            !request2.IsTransformationEnabled(),
                                            !request2.IsTransformationEnabled(),
                                            header_forward,
                                            request2.session_cookie,
                                            request2.GetRealmSession().get(),
                                            host_and_port, uri);

    return ForwardRequest(method, std::move(*headers), body);
}
