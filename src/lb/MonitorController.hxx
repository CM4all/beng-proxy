/*
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_LB_MONITOR_CONTROLLER_HXX
#define BENG_PROXY_LB_MONITOR_CONTROLLER_HXX

struct pool;
class EventLoop;
class SocketAddress;
struct LbMonitorConfig;
struct LbMonitorClass;
class LbMonitorController;

LbMonitorController *
lb_monitor_new(EventLoop &event_loop, struct pool &pool, const char *name,
               const LbMonitorConfig &config,
               SocketAddress address,
               const LbMonitorClass &class_);

void
lb_monitor_free(LbMonitorController *monitor);

void
lb_monitor_enable(LbMonitorController *monitor);

#endif
