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
 * Definitions for the beng-proxy remote control protocol.
 */

#ifndef BENG_PROXY_CONTROL_HXX
#define BENG_PROXY_CONTROL_HXX

#include <stdint.h>

namespace BengProxy {

enum ControlCommand {
    CONTROL_NOP = 0,

    /**
     * Drop items from the translation cache.
     */
    CONTROL_TCACHE_INVALIDATE = 1,

    /**
     * Re-enable the specified node after a failure, remove all
     * failure/fade states.
     *
     * The payload is the node name according to lb.conf, followed by
     * a colon and the port number.
     */
    CONTROL_ENABLE_NODE = 2,

    /**
     * Fade out the specified node, preparing for its shutdown: the
     * node will only be used for pre-existing sessions that refer
     * to it.
     *
     * The payload is the node name according to lb.conf, followed by
     * a colon and the port number.
     */
    CONTROL_FADE_NODE = 3,

    /**
     * Get the status of the specified node.
     *
     * The payload is the node name according to lb.conf, followed by
     * a colon and the port number.
     *
     * The server then sends a response to the source IP.  Its payload
     * is the node name and port, a null byte, and a string describing
     * the worker status.  Possible values: "ok", "error", "fade".
     */
    CONTROL_NODE_STATUS = 4,

    /**
     * Dump all memory pools.
     */
    CONTROL_DUMP_POOLS = 5,

    /**
     * Server statistics.
     */
    CONTROL_STATS = 6,

    /**
     * Set the logger verbosity.  The payload is one byte: 0 means
     * quiet, 1 is the default, and bigger values make the daemon more
     * verbose.
     */
    CONTROL_VERBOSE = 7,

    /**
     * Fade out all child processes (FastCGI, WAS, LHTTP, Delegate;
     * but not beng-proxy workers).  These will not be used for new
     * requests; instead, fresh child processes will be launched.
     * Idle child processes will be killed immediately, and the
     * remaining ones will be killed as soon as their current work is
     * done.
     *
     * If a payload is given, then this is a tag which fades only
     * child processes with the given CHILD_TAG.
     */
    CONTROL_FADE_CHILDREN = 8,

    /**
     * Unregister all Zeroconf services.
     */
    CONTROL_DISABLE_ZEROCONF = 9,

    /**
     * Re-register all Zeroconf services.
     */
    CONTROL_ENABLE_ZEROCONF = 10,

    /**
     * Flush the NFS cache.
     */
    CONTROL_FLUSH_NFS_CACHE = 11,
};

struct ControlStats {
    /**
     * Number of open incoming connections.
     */
    uint32_t incoming_connections;

    /**
     * Number of open outgoing connections.
     */
    uint32_t outgoing_connections;

    /**
     * Number of child processes.
     */
    uint32_t children;

    /**
     * Number of sessions.
     */
    uint32_t sessions;

    /**
     * Total number of incoming HTTP requests that were received since
     * the server was started.
     */
    uint64_t http_requests;

    /**
     * The total allocated size of the translation cache in the
     * server's memory [bytes].
     */
    uint64_t translation_cache_size;

    /**
     * The total allocated size of the HTTP cache in the server's
     * memory [bytes].
     */
    uint64_t http_cache_size;

    /**
     * The total allocated size of the filter cache in the server's
     * memory [bytes].
     */
    uint64_t filter_cache_size;

    uint64_t translation_cache_brutto_size;
    uint64_t http_cache_brutto_size;
    uint64_t filter_cache_brutto_size;

    uint64_t nfs_cache_size, nfs_cache_brutto_size;

    /**
     * Total size of I/O buffers.
     */
    uint64_t io_buffers_size, io_buffers_brutto_size;
};

struct ControlHeader {
    uint16_t length;
    uint16_t command;
};

/**
 * This magic number precedes every UDP packet.
 */
static const uint32_t control_magic = 0x63046101;

} // namespace BengProxy

#endif
