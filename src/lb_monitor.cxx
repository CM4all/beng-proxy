/*
 * Generic monitor class.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "lb_monitor.hxx"
#include "lb_config.hxx"
#include "async.h"
#include "pool.h"
#include "failure.h"

#include <daemon/log.h>

#include <event.h>

struct lb_monitor {
    struct pool *pool;

    const char *name;
    const struct lb_monitor_config *config;
    const struct sockaddr *address;
    size_t address_length;
    const struct lb_monitor_class *class_;

    struct timeval interval;
    struct event interval_event;

    struct timeval timeout;
    struct event timeout_event;

    struct pool_mark_state mark;
    struct async_operation_ref async_ref;

    bool state;
    bool fade;

    lb_monitor(struct pool *_pool, const char *_name,
               const struct lb_monitor_config *_config,
               const struct sockaddr *_address, size_t _address_length,
               const struct lb_monitor_class *_class);

    ~lb_monitor() {
        event_del(&interval_event);

        if (async_ref_defined(&async_ref))
            async_abort(&async_ref);

        pool_unref(pool);
    }
};

static void
monitor_handler_success(void *ctx)
{
    struct lb_monitor *monitor = (struct lb_monitor *)ctx;
    async_ref_clear(&monitor->async_ref);
    evtimer_del(&monitor->timeout_event);

    if (!monitor->state)
        daemon_log(5, "monitor recovered: %s\n", monitor->name);
    else if (monitor->fade)
        daemon_log(5, "monitor finished fade: %s\n", monitor->name);
    else
        daemon_log(6, "monitor ok: %s\n", monitor->name);

    monitor->state = true;

    failure_unset(monitor->address, monitor->address_length,
                  FAILURE_MONITOR);

    if (monitor->fade) {
        monitor->fade = false;
        failure_unset(monitor->address, monitor->address_length,
                      FAILURE_FADE);
    }

    evtimer_add(&monitor->interval_event, &monitor->interval);
}

static void
monitor_handler_fade(void *ctx)
{
    struct lb_monitor *monitor = (struct lb_monitor *)ctx;
    async_ref_clear(&monitor->async_ref);
    evtimer_del(&monitor->timeout_event);

    if (!monitor->fade)
        daemon_log(5, "monitor fade: %s\n", monitor->name);
    else
        daemon_log(6, "monitor still fade: %s\n", monitor->name);

    monitor->fade = true;
    failure_set(monitor->address, monitor->address_length,
                FAILURE_FADE, 300);

    evtimer_add(&monitor->interval_event, &monitor->interval);
}

static void
monitor_handler_timeout(void *ctx)
{
    struct lb_monitor *monitor = (struct lb_monitor *)ctx;
    async_ref_clear(&monitor->async_ref);
    evtimer_del(&monitor->timeout_event);

    daemon_log(monitor->state ? 3 : 6,
               "monitor timeout: %s\n", monitor->name);

    monitor->state = false;
    failure_set(monitor->address, monitor->address_length,
                FAILURE_MONITOR, 0);

    evtimer_add(&monitor->interval_event, &monitor->interval);
}

static void
monitor_handler_error(GError *error, void *ctx)
{
    struct lb_monitor *monitor = (struct lb_monitor *)ctx;
    async_ref_clear(&monitor->async_ref);
    evtimer_del(&monitor->timeout_event);

    if (monitor->state)
        daemon_log(2, "monitor error: %s: %s\n",
                   monitor->name, error->message);
    else
        daemon_log(4, "monitor error: %s: %s\n",
                   monitor->name, error->message);
    g_error_free(error);

    monitor->state = false;
    failure_set(monitor->address, monitor->address_length,
                FAILURE_MONITOR, 0);

    evtimer_add(&monitor->interval_event, &monitor->interval);
}

static const struct lb_monitor_handler monitor_handler = {
    .success = monitor_handler_success,
    .fade = monitor_handler_fade,
    .timeout = monitor_handler_timeout,
    .error = monitor_handler_error,
};

static void
lb_monitor_interval_callback(G_GNUC_UNUSED int fd, G_GNUC_UNUSED short event,
                          void *ctx)
{
    struct lb_monitor *monitor = (struct lb_monitor *)ctx;
    assert(!async_ref_defined(&monitor->async_ref));

    daemon_log(6, "running monitor %s\n", monitor->name);

    if (monitor->config->timeout > 0)
        evtimer_add(&monitor->timeout_event, &monitor->timeout);

    struct pool *pool = pool_new_linear(monitor->pool, "monitor_run", 8192);
    monitor->class_->run(pool, monitor->config,
                         monitor->address, monitor->address_length,
                         &monitor_handler, monitor,
                         &monitor->async_ref);
    pool_unref(pool);
}

static void
lb_monitor_timeout_callback(G_GNUC_UNUSED int fd, G_GNUC_UNUSED short event,
                          void *ctx)
{
    struct lb_monitor *monitor = (struct lb_monitor *)ctx;
    assert(async_ref_defined(&monitor->async_ref));

    daemon_log(6, "monitor timeout: %s\n", monitor->name);

    async_abort(&monitor->async_ref);
    async_ref_clear(&monitor->async_ref);

    monitor->state = false;
    failure_set(monitor->address, monitor->address_length,
                FAILURE_MONITOR, 0);

    evtimer_add(&monitor->interval_event, &monitor->interval);
}

inline
lb_monitor::lb_monitor(struct pool *_pool, const char *_name,
                       const struct lb_monitor_config *_config,
                       const struct sockaddr *_address, size_t _address_length,
                       const struct lb_monitor_class *_class)
    :pool(_pool), name(_name), config(_config),
     address(_address), address_length(_address_length),
     class_(_class),
     interval{config->interval, 0},
     timeout{config->timeout, 0},
     state(true), fade(false) {
    evtimer_set(&interval_event, lb_monitor_interval_callback, this);
    evtimer_set(&timeout_event, lb_monitor_timeout_callback, this);
    async_ref_clear(&async_ref);
    pool_ref(pool);
}

struct lb_monitor *
lb_monitor_new(struct pool *pool, const char *name,
               const struct lb_monitor_config *config,
               const struct sockaddr *address, size_t address_length,
               const struct lb_monitor_class *class_)
{
    return new lb_monitor(pool, name, config,
                          address, address_length,
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
