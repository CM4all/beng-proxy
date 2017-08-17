/*
 * Copyright 2007-2017 Content Management AG
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
 * Definitions for the beng-proxy logging protocol.
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
     * The address of the remote host as a string.
     */
    LOG_REMOTE_HOST,

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

    /**
     * The wallclock duration of the operation as a 64 bit unsigned
     * integer specifying the number of microseconds.
     */
    LOG_DURATION,

    /**
     * The "Host" request header.
     */
    LOG_HOST,
};

/**
 * This magic number precedes every UDP packet.
 */
static const uint32_t log_magic = 0x63046102;

#endif
