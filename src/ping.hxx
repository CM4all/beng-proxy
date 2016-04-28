/*
 * Sending ICMP echo-request messages (ping).
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PING_HXX
#define BENG_PING_HXX

#include "glibfwd.hxx"

#include <inline/compiler.h>

struct PingClientHandler {
    void (*response)(void *ctx);
    void (*timeout)(void *ctx);
    void (*error)(GError *error, void *ctx);
};

struct pool;
class SocketAddress;
struct async_operation_ref;

/**
 * Is the "ping" client available?
 */
gcc_const
bool
ping_available();

/**
 * Sends a "ping" to the server, and waits for the reply.
 */
void
ping(struct pool *pool, SocketAddress address,
     const PingClientHandler &handler, void *ctx,
     struct async_operation_ref *async_ref);

#endif
