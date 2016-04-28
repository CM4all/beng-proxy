/*
 * Sending ICMP echo-request messages (ping).
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PING_HXX
#define BENG_PING_HXX

#include "glibfwd.hxx"

#include <inline/compiler.h>

class PingClientHandler {
public:
    virtual void PingResponse() = 0;
    virtual void PingTimeout() = 0;
    virtual void PingError(GError *error) = 0;
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
     PingClientHandler &handler,
     struct async_operation_ref *async_ref);

#endif
