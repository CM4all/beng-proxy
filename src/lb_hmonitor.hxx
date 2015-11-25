/*
 * Hash table of monitors.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_LB_HMONITOR_H
#define BENG_PROXY_LB_HMONITOR_H

struct pool;
struct LbNodeConfig;
struct LbMonitorConfig;

void
lb_hmonitor_init(struct pool *pool);

void
lb_hmonitor_deinit(void);

void
lb_hmonitor_enable(void);

void
lb_hmonitor_add(const LbNodeConfig *node, unsigned port,
                const LbMonitorConfig *config);

#endif
