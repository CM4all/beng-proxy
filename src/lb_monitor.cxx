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

struct LbMonitor final : public LbMonitorHandler {
    struct pool &pool;

    const char *const name;
    const LbMonitorConfig &config;
    const AllocatedSocketAddress address;
    const struct lb_monitor_class &class_;

    const struct timeval interval;
    TimerEvent interval_event;

    const struct timeval timeout;
    TimerEvent timeout_event;

    struct async_operation_ref async_ref;

    bool state = true;
    bool fade = false;

    LbMonitor(EventLoop &event_loop, struct pool &_pool, const char *_name,
              const LbMonitorConfig &_config,
              SocketAddress _address,
              const struct lb_monitor_class &_class);

    ~LbMonitor() {
        interval_event.Cancel();

        if (async_ref.IsDefined())
            async_ref.Abort();

        pool_unref(&pool);
    }

private:
    void IntervalCallback();
    void TimeoutCallback();

    /* virtual methods from class LbMonitorHandler */
    virtual void Success() override;
    virtual void Fade() override;
    virtual void Timeout() override;
    virtual void Error(GError *error) override;
};

void
LbMonitor::Success()
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
LbMonitor::Fade()
{
    async_ref.Clear();
    timeout_event.Cancel();

    if (!fade)
        daemon_log(5, "monitor fade: %s\n", name);
    else
        daemon_log(6, "monitor still fade: %s\n", name);

    fade = true;
    failure_set(address, FAILURE_FADE, std::chrono::minutes(5));

    interval_event.Add(interval);
}

void
LbMonitor::Timeout()
{
    async_ref.Clear();
    timeout_event.Cancel();

    daemon_log(state ? 3 : 6, "monitor timeout: %s\n", name);

    state = false;
    failure_set(address, FAILURE_MONITOR, std::chrono::seconds::zero());

    interval_event.Add(interval);
}

void
LbMonitor::Error(GError *error)
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
    failure_set(address, FAILURE_MONITOR, std::chrono::seconds::zero());

    interval_event.Add(interval);
}

inline void
LbMonitor::IntervalCallback()
{
    assert(!async_ref.IsDefined());

    daemon_log(6, "running monitor %s\n", name);

    if (config.timeout > 0)
        timeout_event.Add(timeout);

    struct pool *run_pool = pool_new_linear(&pool, "monitor_run", 8192);
    class_.run(run_pool, &config, address, *this, &async_ref);
    pool_unref(run_pool);
}

inline void
LbMonitor::TimeoutCallback()
{
    assert(async_ref.IsDefined());

    daemon_log(6, "monitor timeout: %s\n", name);

    async_ref.AbortAndClear();

    state = false;
    failure_set(address, FAILURE_MONITOR, std::chrono::seconds::zero());

    interval_event.Add(interval);
}

inline
LbMonitor::LbMonitor(EventLoop &event_loop,
                     struct pool &_pool, const char *_name,
                     const LbMonitorConfig &_config,
                     SocketAddress _address,
                     const struct lb_monitor_class &_class)
    :pool(_pool), name(_name), config(_config),
     address(_address),
     class_(_class),
     interval{time_t(config.interval), 0},
     interval_event(event_loop, BIND_THIS_METHOD(IntervalCallback)),
     timeout{time_t(config.timeout), 0},
     timeout_event(event_loop, BIND_THIS_METHOD(TimeoutCallback))
{
    async_ref.Clear();
    pool_ref(&pool);
}

LbMonitor *
lb_monitor_new(EventLoop &event_loop, struct pool &pool, const char *name,
               const LbMonitorConfig &config,
               SocketAddress address,
               const struct lb_monitor_class &class_)
{
    return new LbMonitor(event_loop, pool, name, config,
                         address,
                         class_);
}

void
lb_monitor_free(LbMonitor *monitor)
{
    delete monitor;
}

void
lb_monitor_enable(LbMonitor *monitor)
{
    static constexpr struct timeval immediately = { 0, 0 };
    monitor->interval_event.Add(immediately);
}

bool
lb_monitor_get_state(const LbMonitor *monitor)
{
    return monitor->state;
}
