/*
 * Which headers should be forwarded to/from remote HTTP servers?
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_LB_HEADERS_HXX
#define BENG_LB_HEADERS_HXX

struct pool;
class StringMap;
class HttpHeaders;

void
lb_forward_request_headers(struct pool &pool, StringMap &headers,
                           const char *local_host, const char *remote_host,
                           bool https,
                           const char *peer_subject,
                           const char *peer_issuer_subject,
                           bool mangle_via);

#endif
