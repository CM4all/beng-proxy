/*
 * Common request forwarding code for the request handlers.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_REQUEST_FORWARD_HXX
#define BENG_PROXY_REQUEST_FORWARD_HXX

#include <http/method.h>

class StringMap;
struct header_forward_settings;
struct Request;
class Istream;

struct ForwardRequest {
    http_method_t method;

    StringMap *headers;

    Istream *body;
};

void
request_forward(ForwardRequest &dest, Request &src,
                const struct header_forward_settings &header_forward,
                const char *host_and_port, const char *uri,
                bool exclude_host);

#endif
