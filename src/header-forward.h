/*
 * Which headers should be forwarded to/from remote HTTP servers?
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef HEADER_FORWARD_H
#define HEADER_FORWARD_H

#include "pool.h"

struct session;

struct strmap *
forward_request_headers(pool_t pool, struct strmap *src,
                        const char *local_host, const char *remote_host,
                        bool with_body, bool forward_charset,
                        const struct session *session,
                        const char *host_and_port, const char *uri);

struct growing_buffer *
forward_print_response_headers(pool_t pool, struct strmap *src);

#endif
