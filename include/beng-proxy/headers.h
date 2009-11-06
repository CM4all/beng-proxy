/*
 * Definitions for the beng-proxy translation protocol.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_HEADERS_H
#define BENG_PROXY_HEADERS_H

/**
 * How is a specific set of headers forwarded?
 */
enum beng_header_forward_mode {
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

#endif
