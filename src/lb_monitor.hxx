/*
 * Generic monitor class.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_LB_MONITOR_H
#define BENG_PROXY_LB_MONITOR_H

#include "gerror.h"

#include <inline/compiler.h>

#include <stddef.h>

struct pool;
struct sockaddr;
struct async_operation_ref;
struct lb_monitor_config;

class LBMonitorHandler {
public:
    virtual void Success() = 0;
    virtual void Fade() = 0;
    virtual void Timeout() = 0;
    virtual void Error(GError *error) = 0;
};

struct lb_monitor_class {
    void (*run)(struct pool *pool, const struct lb_monitor_config *config,
                const struct sockaddr *address, size_t address_length,
                LBMonitorHandler &handler,
                struct async_operation_ref *async_ref);
};

struct lb_monitor *
lb_monitor_new(struct pool *pool, const char *name,
               const struct lb_monitor_config *config,
               const struct sockaddr *address, size_t address_length,
               const struct lb_monitor_class *class_);

void
lb_monitor_free(struct lb_monitor *monitor);

void
lb_monitor_enable(struct lb_monitor *monitor);

gcc_pure
bool
lb_monitor_get_state(const struct lb_monitor *monitor);

#endif
