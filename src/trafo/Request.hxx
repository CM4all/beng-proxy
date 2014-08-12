/*
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef TRAFO_REQUEST_HXX
#define TRAFO_REQUEST_HXX

#include <stddef.h>

struct TrafoRequest {
    unsigned protocol_version;

    const char *uri;

    const char *host;

    const char *args, *query_string;

    const char *user_agent;
    const char *ua_class;
    const char *accept_language;

    /**
     * The value of the "Authorization" HTTP request header.
     */
    const char *authorization;

    void Clear() {
        protocol_version = 0;
        uri = nullptr;
        host = nullptr;
        args = query_string = nullptr;
        user_agent = ua_class = accept_language = nullptr;
        authorization = nullptr;
    }
};

#endif
