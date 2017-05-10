/*
 * Generic monitor class.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_LB_MONITOR_HXX
#define BENG_PROXY_LB_MONITOR_HXX

#include "glibfwd.hxx"

#include <inline/compiler.h>

struct pool;
class EventLoop;
class SocketAddress;
class CancellablePointer;
struct LbMonitorConfig;
struct LbMonitor;

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

LbMonitor *
lb_monitor_new(EventLoop &event_loop, struct pool &pool, const char *name,
               const LbMonitorConfig &config,
               SocketAddress address,
               const LbMonitorClass &class_);

void
lb_monitor_free(LbMonitor *monitor);

void
lb_monitor_enable(LbMonitor *monitor);

gcc_pure
bool
lb_monitor_get_state(const LbMonitor *monitor);

#endif
