/*
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef PING_HXX
#define PING_HXX

#include <inline/compiler.h>

#include <exception>

class PingClientHandler {
public:
    virtual void PingResponse() = 0;
    virtual void PingTimeout() = 0;
    virtual void PingError(std::exception_ptr ep) = 0;
};

struct pool;
class EventLoop;
class SocketAddress;
class CancellablePointer;

/**
 * Is the "ping" client available?
 */
gcc_const
bool
ping_available();

/**
 * Sends a "ping" (ICMP echo-request) to the server, and waits for the
 * reply.
 */
void
ping(EventLoop &event_loop, struct pool &pool, SocketAddress address,
     PingClientHandler &handler,
     CancellablePointer &cancel_ptr);

#endif
