/*
 * Generic monitor class.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "lb_monitor.hxx"
#include "lb_config.hxx"
#include "async.h"
#include "pool.h"
#include "failure.hxx"
#include "net/SocketAddress.hxx"

#include <daemon/log.h>

#include <event.h>

struct lb_monitor final : public LBMonitorHandler {
    struct pool *pool;

    const char *name;
    const struct lb_monitor_config *config;
    SocketAddress address;
    const struct lb_monitor_class *class_;

    struct timeval interval;
    struct event interval_event;

    struct timeval timeout;
    struct event timeout_event;

    struct async_operation_ref async_ref;

    bool state;
    bool fade;

    lb_monitor(struct pool *_pool, const char *_name,
               const struct lb_monitor_config *_config,
               SocketAddress _address,
               const struct lb_monitor_class *_class);

    ~lb_monitor() {
        event_del(&interval_event);

        if (async_ref_defined(&async_ref))
            async_abort(&async_ref);

        pool_unref(pool);
    }

    /* virtual methods from class LBMonitorHandler */
    virtual void Success() override;
    virtual void Fade() override;
    virtual void Timeout() override;
    virtual void Error(GError *error) override;
};

void
lb_monitor::Success()
{
    async_ref_clear(&async_ref);
    evtimer_del(&timeout_event);

    if (!state)
        daemon_log(5, "monitor recovered: %s\n", name);
    else if (fade)
        daemon_log(5, "monitor finished fade: %s\n", name);
    else
        daemon_log(6, "monitor ok: %s\n", name);

    state = true;

    failure_unset(address, address.GetSize(), FAILURE_MONITOR);

    if (fade) {
        fade = false;
        failure_unset(address, address.GetSize(), FAILURE_FADE);
    }

    evtimer_add(&interval_event, &interval);
}

void
lb_monitor::Fade()
{
    async_ref_clear(&async_ref);
    evtimer_del(&timeout_event);

    if (!fade)
        daemon_log(5, "monitor fade: %s\n", name);
    else
        daemon_log(6, "monitor still fade: %s\n", name);

    fade = true;
    failure_set(address, address.GetSize(), FAILURE_FADE, 300);

    evtimer_add(&interval_event, &interval);
}

void
lb_monitor::Timeout()
{
    async_ref_clear(&async_ref);
    evtimer_del(&timeout_event);

    daemon_log(state ? 3 : 6, "monitor timeout: %s\n", name);

    state = false;
    failure_set(address, address.GetSize(), FAILURE_MONITOR, 0);

    evtimer_add(&interval_event, &interval);
}

void
lb_monitor::Error(GError *error)
{
    async_ref_clear(&async_ref);
    evtimer_del(&timeout_event);

    if (state)
        daemon_log(2, "monitor error: %s: %s\n",
                   name, error->message);
    else
        daemon_log(4, "monitor error: %s: %s\n",
                   name, error->message);
    g_error_free(error);

    state = false;
    failure_set(address, address.GetSize(), FAILURE_MONITOR, 0);

    evtimer_add(&interval_event, &interval);
}

static void
lb_monitor_interval_callback(gcc_unused int fd, gcc_unused short event,
                          void *ctx)
{
    struct lb_monitor *monitor = (struct lb_monitor *)ctx;
    assert(!async_ref_defined(&monitor->async_ref));

    daemon_log(6, "running monitor %s\n", monitor->name);

    if (monitor->config->timeout > 0)
        evtimer_add(&monitor->timeout_event, &monitor->timeout);

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
    struct lb_monitor *monitor = (struct lb_monitor *)ctx;
    assert(async_ref_defined(&monitor->async_ref));

    daemon_log(6, "monitor timeout: %s\n", monitor->name);

    async_abort(&monitor->async_ref);
    async_ref_clear(&monitor->async_ref);

    monitor->state = false;
    failure_set(monitor->address, monitor->address.GetSize(),
                FAILURE_MONITOR, 0);

    evtimer_add(&monitor->interval_event, &monitor->interval);
}

inline
lb_monitor::lb_monitor(struct pool *_pool, const char *_name,
                       const struct lb_monitor_config *_config,
                       SocketAddress _address,
                       const struct lb_monitor_class *_class)
    :pool(_pool), name(_name), config(_config),
     address(_address),
     class_(_class),
     interval{time_t(config->interval), 0},
     timeout{time_t(config->timeout), 0},
     state(true), fade(false) {
    evtimer_set(&interval_event, lb_monitor_interval_callback, this);
    evtimer_set(&timeout_event, lb_monitor_timeout_callback, this);
    async_ref_clear(&async_ref);
    pool_ref(pool);
}

struct lb_monitor *
lb_monitor_new(struct pool *pool, const char *name,
               const struct lb_monitor_config *config,
               SocketAddress address,
               const struct lb_monitor_class *class_)
{
    return new lb_monitor(pool, name, config,
                          address,
                          class_);
}

void
lb_monitor_free(struct lb_monitor *monitor)
{
    delete monitor;
}

void
lb_monitor_enable(struct lb_monitor *monitor)
{
    static constexpr struct timeval immediately = { 0, 0 };
    evtimer_add(&monitor->interval_event, &immediately);
}

bool
lb_monitor_get_state(const struct lb_monitor *monitor)
{
    return monitor->state;
}
