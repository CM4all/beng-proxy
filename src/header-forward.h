/*
 * Which headers should be forwarded to/from remote HTTP servers?
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef HEADER_FORWARD_H
#define HEADER_FORWARD_H

#include "pool.h"

#include <beng-proxy/headers.h>

struct header_forward_settings {
    enum beng_header_forward_mode modes[HEADER_GROUP_MAX];
};

struct session;

struct strmap *
forward_request_headers(pool_t pool, struct strmap *src,
                        const char *local_host, const char *remote_host,
                        bool with_body, bool forward_charset,
                        bool forward_encoding,
                        const struct header_forward_settings *settings,
                        const struct session *session,
                        const char *host_and_port, const char *uri);

struct strmap *
forward_response_headers(pool_t pool, struct strmap *src,
                         const char *local_host,
                         const struct header_forward_settings *settings);

#endif
