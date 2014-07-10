/*
 * Which headers should be forwarded to/from remote HTTP servers?
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_LB_HEADERS_H
#define BENG_LB_HEADERS_H

struct pool;
struct session;

const struct strmap *
lb_forward_request_headers(struct pool *pool, const struct strmap *src,
                           const char *local_host, const char *remote_host,
                           const char *peer_subject,
                           const char *peer_issuer_subject,
                           bool mangle_via);

#endif
