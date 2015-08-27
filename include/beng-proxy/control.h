/*
 * Definitions for the beng-proxy remote control protocol.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_CONTROL_H
#define BENG_PROXY_CONTROL_H

#include <stdint.h>

enum beng_control_command {
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
     * No payload.
     */
    CONTROL_FADE_CHILDREN = 8,
};

struct beng_control_stats {
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

struct beng_control_header {
    uint16_t length;
    uint16_t command;
};

/**
 * This magic number precedes every UDP packet.
 */
static const uint32_t control_magic = 0x63046101;

#endif
