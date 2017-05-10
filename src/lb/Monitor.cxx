/*
 * Generic monitor class.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "Monitor.hxx"
#include "lb_config.hxx"
#include "pool.hxx"
#include "failure.hxx"
#include "Logger.hxx"
#include "net/SocketAddress.hxx"
#include "event/TimerEvent.hxx"
#include "util/Cancellable.hxx"

#include <glib.h>

struct LbMonitor final : Logger, public LbMonitorHandler {
    EventLoop &event_loop;
    struct pool &pool;

    const std::string name;
    const LbMonitorConfig &config;
    const AllocatedSocketAddress address;
    const LbMonitorClass &class_;

    const struct timeval interval;
    TimerEvent interval_event;

    const struct timeval timeout;
    TimerEvent timeout_event;

    CancellablePointer cancel_ptr;

    bool state = true;
    bool fade = false;

    LbMonitor(EventLoop &_event_loop, struct pool &_pool, const char *_name,
              const LbMonitorConfig &_config,
              SocketAddress _address,
              const LbMonitorClass &_class);

    ~LbMonitor() {
        interval_event.Cancel();

        if (cancel_ptr)
            cancel_ptr.Cancel();

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

protected:
    /* virtual methods from class Logger */
    std::string MakeLogName() const noexcept override {
        return "monitor " + name;
    }
};

void
LbMonitor::Success()
{
    cancel_ptr = nullptr;
    timeout_event.Cancel();

    if (!state)
        Log(5, "recovered");
    else if (fade)
        Log(5, "finished fade");
    else
        Log(6, "ok");

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
    cancel_ptr = nullptr;
    timeout_event.Cancel();

    if (!fade)
        Log(5, "fade");
    else
        Log(6, "still fade");

    fade = true;
    failure_set(address, FAILURE_FADE, std::chrono::minutes(5));

    interval_event.Add(interval);
}

void
LbMonitor::Timeout()
{
    cancel_ptr = nullptr;
    timeout_event.Cancel();

    Log(state ? 3 : 6, "timeout");

    state = false;
    failure_set(address, FAILURE_MONITOR, std::chrono::seconds::zero());

    interval_event.Add(interval);
}

void
LbMonitor::Error(GError *error)
{
    cancel_ptr = nullptr;
    timeout_event.Cancel();

    Log(state ? 2 : 4, "error", error);
    g_error_free(error);

    state = false;
    failure_set(address, FAILURE_MONITOR, std::chrono::seconds::zero());

    interval_event.Add(interval);
}

inline void
LbMonitor::IntervalCallback()
{
    assert(!cancel_ptr);

    Log(6, "run");

    if (config.timeout > 0)
        timeout_event.Add(timeout);

    struct pool *run_pool = pool_new_linear(&pool, "monitor_run", 8192);
    class_.run(event_loop, *run_pool, config, address, *this, cancel_ptr);
    pool_unref(run_pool);
}

inline void
LbMonitor::TimeoutCallback()
{
    assert(cancel_ptr);

    Log(6, "timeout");

    cancel_ptr.CancelAndClear();

    state = false;
    failure_set(address, FAILURE_MONITOR, std::chrono::seconds::zero());

    interval_event.Add(interval);
}

inline
LbMonitor::LbMonitor(EventLoop &_event_loop,
                     struct pool &_pool, const char *_name,
                     const LbMonitorConfig &_config,
                     SocketAddress _address,
                     const LbMonitorClass &_class)
    :event_loop(_event_loop), pool(_pool), name(_name), config(_config),
     address(_address),
     class_(_class),
     interval{time_t(config.interval), 0},
     interval_event(event_loop, BIND_THIS_METHOD(IntervalCallback)),
     timeout{time_t(config.timeout), 0},
     timeout_event(event_loop, BIND_THIS_METHOD(TimeoutCallback))
{
    pool_ref(&pool);
}

LbMonitor *
lb_monitor_new(EventLoop &event_loop, struct pool &pool, const char *name,
               const LbMonitorConfig &config,
               SocketAddress address,
               const LbMonitorClass &class_)
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
