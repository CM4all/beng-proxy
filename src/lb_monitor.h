/*
 * Generic monitor class.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_LB_MONITOR_H
#define BENG_PROXY_LB_MONITOR_H

#include <glib.h>
#include <stdbool.h>

struct pool;
struct address_envelope;
struct async_operation_ref;

struct lb_monitor_handler {
    void (*success)(void *ctx);
    void (*timeout)(void *ctx);
    void (*error)(GError *error, void *ctx);
};

struct lb_monitor_class {
    void (*run)(struct pool *pool, const struct address_envelope *envelope,
                const struct lb_monitor_handler *handler, void *handler_ctx,
                struct async_operation_ref *async_ref);
};

struct lb_monitor *
lb_monitor_new(struct pool *pool, const char *name,
               const struct address_envelope *envelope,
               const struct lb_monitor_class *class);

void
lb_monitor_free(struct lb_monitor *monitor);

G_GNUC_PURE
bool
lb_monitor_get_state(const struct lb_monitor *monitor);

#endif
