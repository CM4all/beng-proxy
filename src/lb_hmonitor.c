/*
 * Hash table of monitors.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "lb_hmonitor.h"
#include "lb_monitor.h"
#include "lb_ping_monitor.h"
#include "lb_config.h"
#include "pool.h"
#include "tpool.h"
#include "hashmap.h"

static struct pool *hmonitor_pool;
static struct hashmap *hmonitor_map;

void
lb_hmonitor_init(struct pool *pool)
{
    hmonitor_pool = pool_new_linear(pool, "hmonitor", 4096);
    hmonitor_map = hashmap_new(hmonitor_pool, 59);
}

void
lb_hmonitor_deinit(void)
{
    hashmap_rewind(hmonitor_map);
    const struct hashmap_pair *pair;
    while ((pair = hashmap_next(hmonitor_map)) != NULL) {
        struct lb_monitor *monitor = pair->value;
        lb_monitor_free(monitor);
    }

    pool_unref(hmonitor_pool);
}

void
lb_hmonitor_add(const struct lb_node_config *node, unsigned port,
                const struct lb_monitor_config *config)
{
    const struct lb_monitor_class *class;
    switch (config->type) {
    case MONITOR_NONE:
        /* nothing to do */
        return;

    case MONITOR_PING:
        class = &ping_monitor_class;
        break;
    }

    struct pool_mark mark;
    pool_mark(tpool, &mark);
    const char *key = p_sprintf(tpool, "%s:[%s]:%u",
                                config->name, node->name, port);
    struct lb_monitor *monitor = hashmap_get(hmonitor_map, key);
    if (monitor == NULL) {
        /* doesn't exist yet: create it */
        struct pool *pool = pool_new_linear(hmonitor_pool, "monitor", 1024);
        key = p_strdup(pool, key);
        monitor = lb_monitor_new(pool, key, node->envelope, class);
        pool_unref(pool);
        hashmap_add(hmonitor_map, key, monitor);
    }

    pool_rewind(tpool, &mark);
}
