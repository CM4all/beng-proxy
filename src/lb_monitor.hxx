/*
 * Generic monitor class.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_LB_MONITOR_H
#define BENG_PROXY_LB_MONITOR_H

#include "glibfwd.hxx"

#include <inline/compiler.h>

struct pool;
class EventLoop;
class SocketAddress;
struct async_operation_ref;
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
    void (*run)(struct pool *pool, const LbMonitorConfig *config,
                SocketAddress address,
                LbMonitorHandler &handler,
                struct async_operation_ref *async_ref);
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
