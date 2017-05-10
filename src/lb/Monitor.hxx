/*
 * Generic monitor class.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_LB_MONITOR_HXX
#define BENG_PROXY_LB_MONITOR_HXX

#include "glibfwd.hxx"

struct pool;
class EventLoop;
class SocketAddress;
class CancellablePointer;
struct LbMonitorConfig;

class LbMonitorHandler {
public:
    virtual void Success() = 0;
    virtual void Fade() = 0;
    virtual void Timeout() = 0;
    virtual void Error(GError *error) = 0;
};

struct LbMonitorClass {
    void (*run)(EventLoop &event_loop,
                struct pool &pool, const LbMonitorConfig &config,
                SocketAddress address,
                LbMonitorHandler &handler,
                CancellablePointer &cancel_ptr);
};

#endif
