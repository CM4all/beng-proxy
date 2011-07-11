/*
 * Hash table of monitors.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_LB_HMONITOR_H
#define BENG_PROXY_LB_HMONITOR_H

struct pool;
struct lb_node_config;
struct lb_monitor_config;

void
lb_hmonitor_init(struct pool *pool);

void
lb_hmonitor_deinit(void);

void
lb_hmonitor_add(const struct lb_node_config *node, unsigned port,
                const struct lb_monitor_config *config);

#endif
