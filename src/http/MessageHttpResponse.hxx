/*
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef MESSAGE_HTTP_RESPONSE_HXX
#define MESSAGE_HTTP_RESPONSE_HXX

#include <http/status.h>

/**
 * Describes a very simple HTTP response with a text/plain body.
 */
struct MessageHttpResponse {
    http_status_t status;

    /**
     * The response body.  This string must either be a literal or the
     * entity which constructs this object must ensure that it will be
     * valid until sending the response has finished (e.g. by
     * allocating on the #HttpServerRequest pool).
     */
    const char *message;
};

#endif
