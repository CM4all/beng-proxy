/*
 * Generic monitor class.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "lb_monitor.hxx"
#include "lb_config.hxx"
#include "async.hxx"
#include "pool.hxx"
#include "failure.hxx"
#include "net/SocketAddress.hxx"
#include "event/TimerEvent.hxx"

#include <daemon/log.h>

struct LBMonitor final : public LBMonitorHandler {
    struct pool *pool;

    const char *name;
    const LbMonitorConfig *config;
    SocketAddress address;
    const struct lb_monitor_class *class_;

    struct timeval interval;
    TimerEvent interval_event;

    struct timeval timeout;
    TimerEvent timeout_event;

    struct async_operation_ref async_ref;

    bool state;
    bool fade;

    LBMonitor(struct pool *_pool, const char *_name,
              const LbMonitorConfig *_config,
              SocketAddress _address,
              const struct lb_monitor_class *_class);

    ~LBMonitor() {
        interval_event.Cancel();

        if (async_ref.IsDefined())
            async_ref.Abort();

        pool_unref(pool);
    }

    /* virtual methods from class LBMonitorHandler */
    virtual void Success() override;
    virtual void Fade() override;
    virtual void Timeout() override;
    virtual void Error(GError *error) override;
};

void
LBMonitor::Success()
{
    async_ref.Clear();
    timeout_event.Cancel();

    if (!state)
        daemon_log(5, "monitor recovered: %s\n", name);
    else if (fade)
        daemon_log(5, "monitor finished fade: %s\n", name);
    else
        daemon_log(6, "monitor ok: %s\n", name);

    state = true;

    failure_unset(address, FAILURE_MONITOR);

    if (fade) {
        fade = false;
        failure_unset(address, FAILURE_FADE);
    }

    interval_event.Add(interval);
}

void
LBMonitor::Fade()
{
    async_ref.Clear();
    timeout_event.Cancel();

    if (!fade)
        daemon_log(5, "monitor fade: %s\n", name);
    else
        daemon_log(6, "monitor still fade: %s\n", name);

    fade = true;
    failure_set(address, FAILURE_FADE, 300);

    interval_event.Add(interval);
}

void
LBMonitor::Timeout()
{
    async_ref.Clear();
    timeout_event.Cancel();

    daemon_log(state ? 3 : 6, "monitor timeout: %s\n", name);

    state = false;
    failure_set(address, FAILURE_MONITOR, 0);

    interval_event.Add(interval);
}

void
LBMonitor::Error(GError *error)
{
    async_ref.Clear();
    timeout_event.Cancel();

    if (state)
        daemon_log(2, "monitor error: %s: %s\n",
                   name, error->message);
    else
        daemon_log(4, "monitor error: %s: %s\n",
                   name, error->message);
    g_error_free(error);

    state = false;
    failure_set(address, FAILURE_MONITOR, 0);

    interval_event.Add(interval);
}

static void
lb_monitor_interval_callback(gcc_unused int fd, gcc_unused short event,
                          void *ctx)
{
    LBMonitor *monitor = (LBMonitor *)ctx;
    assert(!monitor->async_ref.IsDefined());

    daemon_log(6, "running monitor %s\n", monitor->name);

    if (monitor->config->timeout > 0)
        monitor->timeout_event.Add(monitor->timeout);

    struct pool *pool = pool_new_linear(monitor->pool, "monitor_run", 8192);
    monitor->class_->run(pool, monitor->config,
                         monitor->address,
                         *monitor,
                         &monitor->async_ref);
    pool_unref(pool);
}

static void
lb_monitor_timeout_callback(gcc_unused int fd, gcc_unused short event,
                            void *ctx)
{
    LBMonitor *monitor = (LBMonitor *)ctx;
    assert(monitor->async_ref.IsDefined());

    daemon_log(6, "monitor timeout: %s\n", monitor->name);

    monitor->async_ref.Abort();
    monitor->async_ref.Clear();

    monitor->state = false;
    failure_set(monitor->address, FAILURE_MONITOR, 0);

    monitor->interval_event.Add(monitor->interval);
}

inline
LBMonitor::LBMonitor(struct pool *_pool, const char *_name,
                     const LbMonitorConfig *_config,
                     SocketAddress _address,
                     const struct lb_monitor_class *_class)
    :pool(_pool), name(_name), config(_config),
     address(_address),
     class_(_class),
     interval{time_t(config->interval), 0},
     interval_event(lb_monitor_interval_callback, this),
     timeout{time_t(config->timeout), 0},
     timeout_event(lb_monitor_timeout_callback, this),
     state(true), fade(false) {
         async_ref.Clear();
         pool_ref(pool);
     }

LBMonitor *
lb_monitor_new(struct pool *pool, const char *name,
               const LbMonitorConfig *config,
               SocketAddress address,
               const struct lb_monitor_class *class_)
{
    return new LBMonitor(pool, name, config,
                         address,
                         class_);
}

void
lb_monitor_free(LBMonitor *monitor)
{
    delete monitor;
}

void
lb_monitor_enable(LBMonitor *monitor)
{
    static constexpr struct timeval immediately = { 0, 0 };
    monitor->interval_event.Add(immediately);
}

bool
lb_monitor_get_state(const LBMonitor *monitor)
{
    return monitor->state;
}
