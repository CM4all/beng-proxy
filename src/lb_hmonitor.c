/*
 * Hash table of monitors.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "lb_hmonitor.h"
#include "lb_monitor.h"
#include "lb_ping_monitor.h"
#include "lb_syn_monitor.h"
#include "lb_expect_monitor.h"
#include "lb_config.h"
#include "pool.h"
#include "tpool.h"
#include "hashmap.h"
#include "address-envelope.h"
#include "address-edit.h"

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
lb_hmonitor_enable(void)
{
    hashmap_rewind(hmonitor_map);
    const struct hashmap_pair *pair;
    while ((pair = hashmap_next(hmonitor_map)) != NULL) {
        struct lb_monitor *monitor = pair->value;
        lb_monitor_enable(monitor);
    }
}

void
lb_hmonitor_add(const struct lb_node_config *node, unsigned port,
                const struct lb_monitor_config *config)
{
    const struct lb_monitor_class *class = NULL;
    switch (config->type) {
    case MONITOR_NONE:
        /* nothing to do */
        return;

    case MONITOR_PING:
        class = &ping_monitor_class;
        break;

    case MONITOR_CONNECT:
        class = &syn_monitor_class;
        break;

    case MONITOR_TCP_EXPECT:
        class = &expect_monitor_class;
        break;
    }

    assert(class != NULL);

    struct pool_mark mark;
    pool_mark(tpool, &mark);
    const char *key = p_sprintf(tpool, "%s:[%s]:%u",
                                config->name, node->name, port);
    struct lb_monitor *monitor = hashmap_get(hmonitor_map, key);
    if (monitor == NULL) {
        /* doesn't exist yet: create it */
        struct pool *pool = pool_new_linear(hmonitor_pool, "monitor", 1024);
        key = p_strdup(pool, key);

        const struct sockaddr *address = &node->envelope->address;
        if (port > 0)
            address = sockaddr_set_port(pool, address,
                                        node->envelope->length, port);

        monitor = lb_monitor_new(pool, key, config,
                                 address, node->envelope->length,
                                 class);
        pool_unref(pool);
        hashmap_add(hmonitor_map, key, monitor);
    }

    pool_rewind(tpool, &mark);
}
