/*
 * Which headers should be forwarded to/from remote HTTP servers?
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef HEADER_FORWARD_HXX
#define HEADER_FORWARD_HXX

#include <beng-proxy/headers.h>

struct header_forward_settings {
    enum beng_header_forward_mode modes[HEADER_GROUP_MAX];
};

struct pool;
struct session;

/**
 * @param exclude_host suppress the "Host" header?  The "Host" request
 * header must not be forwarded to another HTTP server, because we
 * need to generate a new one
 */
struct strmap *
forward_request_headers(struct pool *pool, struct strmap *src,
                        const char *local_host, const char *remote_host,
                        bool exclude_host,
                        bool with_body, bool forward_charset,
                        bool forward_encoding,
                        const struct header_forward_settings *settings,
                        const char *session_cookie,
                        const struct session *session,
                        const char *host_and_port, const char *uri);

struct strmap *
forward_response_headers(struct pool *pool, struct strmap *src,
                         const char *local_host,
                         const char *session_cookie,
                         const struct header_forward_settings *settings);

#endif
