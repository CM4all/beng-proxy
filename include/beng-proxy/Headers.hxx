/*
 * Copyright 2007-2018 Content Management AG
 * All rights reserved.
 *
 * author: Max Kellermann <mk@cm4all.com>
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * - Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 *
 * - Redistributions in binary form must reproduce the above copyright
 * notice, this list of conditions and the following disclaimer in the
 * documentation and/or other materials provided with the
 * distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE
 * FOUNDATION OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED
 * OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/*
 * Definitions for the beng-proxy translation protocol.
 */

#ifndef BENG_PROXY_HEADERS_HXX
#define BENG_PROXY_HEADERS_HXX

#include <stdint.h>

namespace BengProxy {

/**
 * How is a specific set of headers forwarded?
 */
enum class HeaderForwardMode {
    /**
     * Do not forward at all.
     */
    NO,

    /**
     * Forward it as-is.
     */
    YES,

    /**
     * Forward it, but mangle it.  Example: cookie headers are handled
     * by beng-proxy.
     */
    MANGLE,

    /**
     * A special "mixed" mode where both beng-proxy and the backend
     * server handle certain headers.
     */
    BOTH,
};

/**
 * Selectors for a group of headers.
 */
enum class HeaderGroup {
    /**
     * Special value for "override all settings" (except for
     * #SECURE and #LINK).
     */
    ALL = -1,

    /**
     * Reveal the identity of the real communication partner?  This
     * affects "Via", "X-Forwarded-For".
     */
    IDENTITY,

    /**
     * Forward headers showing the capabilities of the real
     * communication partner?  Affects "Server", "User-Agent",
     * "Accept-*" and others.
     *
     * Note that the "Server" response header is always sent, even
     * when this attribute is set to #HEADER_FORWARD_NO.
     */
    CAPABILITIES,

    /**
     * Forward cookie headers?
     */
    COOKIE,

    /**
     * Forwarding mode for "other" headers: headers not explicitly
     * handled here.  This does not include hop-by-hop headers.
     */
    OTHER,

    /**
     * Forward information about the original request/response that
     * would usually not be visible.  If set to
     * #HEADER_FORWARD_MANGLE, then "Host" is translated to
     * "X-Forwarded-Host".
     */
    FORWARD,

    /**
     * Forward CORS headers.
     *
     * @see http://www.w3.org/TR/cors/#syntax
     */
    CORS,

    /**
     * Forward "secure" headers such as "x-cm4all-beng-user".
     */
    SECURE,

    /**
     * Forward headers that affect the transformation, such as
     * "x-cm4all-view".
     */
    TRANSFORMATION,

    /**
     * Forward headers that contain links, such as "location".
     */
    LINK,

    /**
     * Information about the SSL connection,
     * i.e. X-CM4all-BENG-Peer-Subject and
     * X-CM4all-BENG-Peer-Issuer-Subject.
     */
    SSL,

    /**
     * Internal definition for estimating the size of an array.
     */
    MAX,
};

struct HeaderForwardPacket {
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
};

static_assert(sizeof(HeaderForwardPacket) == 4);

} // namespace BengProxy

#endif
