/*
 * Which headers should be forwarded to/from remote HTTP servers?
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef HEADER_FORWARD_HXX
#define HEADER_FORWARD_HXX

#include <beng-proxy/headers.h>

#include <http/status.h>

struct header_forward_settings {
    enum beng_header_forward_mode modes[HEADER_GROUP_MAX];
};

struct pool;
class StringMap;
struct RealmSession;

/**
 * @param exclude_host suppress the "Host" header?  The "Host" request
 * header must not be forwarded to another HTTP server, because we
 * need to generate a new one
 * @param forward_range forward the "Range" request header?
 */
StringMap
forward_request_headers(struct pool &pool, const StringMap &src,
                        const char *local_host, const char *remote_host,
                        bool exclude_host,
                        bool with_body, bool forward_charset,
                        bool forward_encoding,
                        bool forward_range,
                        const struct header_forward_settings &settings,
                        const char *session_cookie,
                        const RealmSession *session,
                        const char *host_and_port, const char *uri);

StringMap
forward_response_headers(struct pool &pool, http_status_t status,
                         const StringMap &src,
                         const char *local_host,
                         const char *session_cookie,
                         const char *(*relocate)(const char *uri, void *ctx),
                         void *relocate_ctx,
                         const struct header_forward_settings &settings);

/**
 * Generate a X-CM4all-BENG-User header (if available)_.
 */
void
forward_reveal_user(StringMap &headers,
                    const RealmSession *session);

#endif
