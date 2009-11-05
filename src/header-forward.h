/*
 * Which headers should be forwarded to/from remote HTTP servers?
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef HEADER_FORWARD_H
#define HEADER_FORWARD_H

#include "pool.h"

/**
 * How is a specific set of headers forwarded?
 */
enum header_forward_mode {
    /**
     * Do not forward at all.
     */
    HEADER_FORWARD_NO,

    /**
     * Forward it as-is.
     */
    HEADER_FORWARD_YES,

    /**
     * Forward it, but mangle it.  Example: cookie headers are handled
     * by
     */
    HEADER_FORWARD_MANGLE,
};

struct header_forward_settings {
    /**
     * Reveal the identity of the real communication partner?  This
     * affects "Via", "X-Forwarded-For".
     */
    enum header_forward_mode identity;

    /**
     * Forward headers showing the capabilities of the real
     * communication partner?  Affects "Server", "User-Agent",
     * "Accept-*" and others.
     *
     * Note that the "Server" response header is always sent, even
     * when this attribute is set to #HEADER_FORWARD_NO.
     */
    enum header_forward_mode capabilities;

    /**
     * Forward cookie headers?
     */
    enum header_forward_mode cookie;

    /**
     * Forwarding mode for "other" headers: headers not explicitly
     * handled here.  This does not include hop-by-hop headers.
     */
    enum header_forward_mode other;
};

struct session;

struct strmap *
forward_request_headers(pool_t pool, struct strmap *src,
                        const char *local_host, const char *remote_host,
                        bool with_body, bool forward_charset,
                        const struct header_forward_settings *settings,
                        const struct session *session,
                        const char *host_and_port, const char *uri);

struct growing_buffer *
forward_print_response_headers(pool_t pool, struct strmap *src);

#endif
