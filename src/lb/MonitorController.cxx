/*
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "MonitorController.hxx"
#include "MonitorConfig.hxx"
#include "pool.hxx"
#include "failure.hxx"

void
LbMonitorController::Success()
{
    cancel_ptr = nullptr;
    timeout_event.Cancel();

    if (!state)
        logger(5, "recovered");
    else if (fade)
        logger(5, "finished fade");
    else
        logger(6, "ok");

    state = true;

    failure_unset(address, FAILURE_MONITOR);

    if (fade) {
        fade = false;
        failure_unset(address, FAILURE_FADE);
    }

    interval_event.Add(interval);
}

void
LbMonitorController::Fade()
{
    cancel_ptr = nullptr;
    timeout_event.Cancel();

    if (!fade)
        logger(5, "fade");
    else
        logger(6, "still fade");

    fade = true;
    failure_set(address, FAILURE_FADE, std::chrono::minutes(5));

    interval_event.Add(interval);
}

void
LbMonitorController::Timeout()
{
    cancel_ptr = nullptr;
    timeout_event.Cancel();

    logger(state ? 3 : 6, "timeout");

    state = false;
    failure_set(address, FAILURE_MONITOR, std::chrono::seconds::zero());

    interval_event.Add(interval);
}

void
LbMonitorController::Error(std::exception_ptr e)
{
    cancel_ptr = nullptr;
    timeout_event.Cancel();

    logger(state ? 2 : 4, "error: ", e);

    state = false;
    failure_set(address, FAILURE_MONITOR, std::chrono::seconds::zero());

    interval_event.Add(interval);
}

inline void
LbMonitorController::IntervalCallback()
{
    assert(!cancel_ptr);

    logger(6, "run");

    if (config.timeout > 0)
        timeout_event.Add(timeout);

    struct pool *run_pool = pool_new_linear(&pool, "monitor_run", 8192);
    class_.run(event_loop, *run_pool, config, address, *this, cancel_ptr);
    pool_unref(run_pool);
}

inline void
LbMonitorController::TimeoutCallback()
{
    assert(cancel_ptr);

    logger(6, "timeout");

    cancel_ptr.CancelAndClear();

    state = false;
    failure_set(address, FAILURE_MONITOR, std::chrono::seconds::zero());

    interval_event.Add(interval);
}

LbMonitorController::LbMonitorController(EventLoop &_event_loop,
                                         struct pool &_pool, const char *_name,
                                         const LbMonitorConfig &_config,
                                         SocketAddress _address,
                                         const LbMonitorClass &_class)
    :event_loop(_event_loop), pool(_pool), name(_name), config(_config),
     address(_address),
     class_(_class),
     logger("monitor " + name),
     interval{time_t(config.interval), 0},
     interval_event(event_loop, BIND_THIS_METHOD(IntervalCallback)),
     timeout{time_t(config.timeout), 0},
     timeout_event(event_loop, BIND_THIS_METHOD(TimeoutCallback))
{
    pool_ref(&pool);
}

LbMonitorController::~LbMonitorController()
{
    interval_event.Cancel();
    timeout_event.Cancel();

    if (cancel_ptr)
        cancel_ptr.Cancel();

    pool_unref(&pool);
}
