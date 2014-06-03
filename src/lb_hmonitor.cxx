/*
 * Hash table of monitors.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "lb_hmonitor.hxx"
#include "lb_monitor.hxx"
#include "lb_ping_monitor.hxx"
#include "lb_syn_monitor.hxx"
#include "lb_expect_monitor.hxx"
#include "lb_config.hxx"
#include "pool.h"
#include "tpool.h"
#include "hashmap.h"
#include "address_envelope.hxx"
#include "address_edit.h"

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
        struct lb_monitor *monitor = (struct lb_monitor *)pair->value;
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
        struct lb_monitor *monitor = (struct lb_monitor *)pair->value;
        lb_monitor_enable(monitor);
    }
}

void
lb_hmonitor_add(const struct lb_node_config *node, unsigned port,
                const struct lb_monitor_config *config)
{
    const struct lb_monitor_class *class_ = nullptr;
    switch (config->type) {
    case lb_monitor_config::Type::NONE:
        /* nothing to do */
        return;

    case lb_monitor_config::Type::PING:
        class_ = &ping_monitor_class;
        break;

    case lb_monitor_config::Type::CONNECT:
        class_ = &syn_monitor_class;
        break;

    case lb_monitor_config::Type::TCP_EXPECT:
        class_ = &expect_monitor_class;
        break;
    }

    assert(class_ != NULL);

    const AutoRewindPool auto_rewind(tpool);

    const char *key = p_sprintf(tpool, "%s:[%s]:%u",
                                config->name.c_str(), node->name.c_str(),
                                port);
    struct lb_monitor *monitor =
        (struct lb_monitor *)hashmap_get(hmonitor_map, key);
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
                                 class_);
        pool_unref(pool);
        hashmap_add(hmonitor_map, key, monitor);
    }
}
