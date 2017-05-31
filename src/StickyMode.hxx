/*
 * Configuration.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_STICKY_MODE_HXX
#define BENG_PROXY_STICKY_MODE_HXX

/**
 * The "sticky" mode specifies which node of a cluster is chosen to
 * handle a request.
 */
enum class StickyMode {
    /**
     * No specific mode, beng-lb may choose nodes at random.
     */
    NONE,

    /**
     * The first non-failing node is used.
     */
    FAILOVER,

    /**
     * Select the node with a hash of the client's IP address.
     */
    SOURCE_IP,

    /**
     * Select the node with a hash of the "Host" request header.
     */
    HOST,

    /**
     * A modulo of the lower 32 bit of the beng-proxy session id is
     * used to determine which worker shall be used.  Requires
     * cooperation from beng-proxy on the nodes.
     */
    SESSION_MODULO,

    /**
     * A cookie is sent to the client, which is later used to direct
     * its requests to the same cluster node.
     */
    COOKIE,

    /**
     * Tomcat with jvmRoute in cookie.
     */
    JVM_ROUTE,
};

#endif
