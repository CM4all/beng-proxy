/*
 * Definitions for the beng-proxy logging protocol.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_LOG_H
#define BENG_PROXY_LOG_H

#include <stdint.h>

enum beng_log_attribute {
    LOG_NULL = 0,

    /**
     * A 64 bit time stamp of the event, microseconds since epoch.
     */
    LOG_TIMESTAMP,

    /**
     * The name of the site which was accessed.
     */
    LOG_SITE,

    /**
     * The request method (http_method_t) as a 8 bit integer.
     */
    LOG_HTTP_METHOD,

    /**
     * The request URI.
     */
    LOG_HTTP_URI,

    /**
     * The "Referer"[sic] request header.
     */
    LOG_HTTP_REFERER,

    /**
     * The "User-Agent" request header.
     */
    LOG_USER_AGENT,

    /**
     * The response status (http_status_t) as a 16 bit integer.
     */
    LOG_HTTP_STATUS,

    /**
     * The netto length of the entity in bytes, as a 64 bit integer.
     */
    LOG_LENGTH,

    /**
     * The total number of raw bytes received and sent for this event,
     * as two 64 bit integers.  This includes all extra data such as
     * headers.
     */
    LOG_TRAFFIC,
};

/**
 * This magic number precedes every UDP packet.
 */
static const uint32_t log_magic = 0x63046102;

#endif
