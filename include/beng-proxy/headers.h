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

/**
 * Selectors for a group of headers.
 */
enum beng_header_group {
    /**
     * Special value for "override all settings".
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
     * Internal definition for estimating the size of an array.
     */
    HEADER_GROUP_MAX,
};

#endif
