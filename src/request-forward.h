/*
 * Common request forwarding code for the request handlers.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_REQUEST_FORWARD_H
#define BENG_PROXY_REQUEST_FORWARD_H

#include <http/method.h>

struct header_forward_settings;
struct request;

struct forward_request {
    http_method_t method;

    struct strmap *headers;

    struct istream *body;
};

#ifdef __cplusplus
extern "C" {
#endif

void
request_forward(struct forward_request *dest, struct request *src,
                const struct header_forward_settings *header_forward,
                const char *host_and_port, const char *uri,
                bool exclude_host);

#ifdef __cplusplus
}
#endif

#endif
