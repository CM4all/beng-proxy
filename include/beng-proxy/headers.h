/*
 * Definitions for the beng-proxy translation protocol.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_HEADERS_H
#define BENG_PROXY_HEADERS_H

#include <inline/compiler.h>

#include <stdint.h>

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
     * by beng-proxy.
     */
    HEADER_FORWARD_MANGLE,

    /**
     * A special "mixed" mode where both beng-proxy and the backend
     * server handle certain headers.
     */
    HEADER_FORWARD_BOTH,
};

/**
 * Selectors for a group of headers.
 */
enum beng_header_group {
    /**
     * Special value for "override all settings" (except for
     * #HEADER_GROUP_SECURE).
     */
    HEADER_GROUP_ALL = -1,

    /**
     * Reveal the identity of the real communication partner?  This
     * affects "Via", "X-Forwarded-For".
     */
    HEADER_GROUP_IDENTITY,

    /**
     * Forward headers showing the capabilities of the real
     * communication partner?  Affects "Server", "User-Agent",
     * "Accept-*" and others.
     *
     * Note that the "Server" response header is always sent, even
     * when this attribute is set to #HEADER_FORWARD_NO.
     */
    HEADER_GROUP_CAPABILITIES,

    /**
     * Forward cookie headers?
     */
    HEADER_GROUP_COOKIE,

    /**
     * Forwarding mode for "other" headers: headers not explicitly
     * handled here.  This does not include hop-by-hop headers.
     */
    HEADER_GROUP_OTHER,

    /**
     * Forward information about the original request/response that
     * would usually not be visible.  If set to
     * #HEADER_FORWARD_MANGLE, then "Host" is translated to
     * "X-Forwarded-Host".
     */
    HEADER_GROUP_FORWARD,

    /**
     * Forward CORS headers.
     *
     * @see http://www.w3.org/TR/cors/#syntax
     */
    HEADER_GROUP_CORS,

    /**
     * Forward "secure" headers such as "x-cm4all-beng-user".
     */
    HEADER_GROUP_SECURE,

    /**
     * Forward headers that affect the transformation, such as
     * "x-cm4all-view".
     */
    HEADER_GROUP_TRANSFORMATION,

    /**
     * Internal definition for estimating the size of an array.
     */
    HEADER_GROUP_MAX,
};

struct beng_header_forward_packet {
    /**
     * See #beng_header_group
     */
    int16_t group;

    /**
     * See #beng_header_forward_mode
     */
    uint8_t mode;

    /**
     * Unused padding byte.  Set 0.
     */
    uint8_t reserved;
} gcc_packed;

#endif
