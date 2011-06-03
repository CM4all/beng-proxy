/*
 * Configuration.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_STICKY_H
#define BENG_PROXY_STICKY_H

/**
 * The "sticky" mode specifies which node of a cluster is chosen to
 * handle a request.
 */
enum sticky_mode {
    /**
     * No specific mode, beng-lb may choose nodes at random.
     */
    STICKY_NONE,

    /**
     * A modulo of the lower 32 bit of the beng-proxy session id is
     * used to determine which worker shall be used.  Requires
     * cooperation from beng-proxy on the nodes.
     */
    STICKY_SESSION_MODULO,
};

#endif
