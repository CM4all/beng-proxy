/*
 * Set up load balancer objects.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "lb_setup.h"
#include "lb_config.h"
#include "lb_instance.h"
#include "lb_listener.h"
#include "lb_hmonitor.h"

static void
init_cluster_monitors(const struct lb_cluster_config *cluster)
{
    if (cluster->monitor == NULL)
        return;

    for (unsigned i = 0; i < cluster->num_members; ++i) {
        const struct lb_member_config *member = &cluster->members[i];

        lb_hmonitor_add(member->node, member->port, cluster->monitor);
    }
}

bool
init_all_listeners(struct lb_instance *instance, GError **error_r)
{
    bool success = true;

    for (struct lb_listener_config *config = (struct lb_listener_config *)instance->config->listeners.next;
         &config->siblings != &instance->config->listeners;
         config = (struct lb_listener_config *)config->siblings.next) {
        struct lb_listener *listener = lb_listener_new(instance, config,
                                                       error_r);
        if (listener == NULL)
            return false;

        list_add(&listener->siblings, &instance->listeners);

        init_cluster_monitors(config->cluster);
    }

    return success;
}

void
deinit_all_listeners(struct lb_instance *instance)
{
    while (!list_empty(&instance->listeners)) {
        struct lb_listener *listener =
            (struct lb_listener *)instance->listeners.next;
        list_remove(&listener->siblings);
        lb_listener_free(listener);
    }
}
