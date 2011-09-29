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
#include "lb_control.h"

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

void
all_listeners_event_add(struct lb_instance *instance)
{
    for (struct lb_listener *l = (struct lb_listener *)instance->listeners.next;
         &l->siblings != &instance->listeners;
         l = (struct lb_listener *)l->siblings.next)
        lb_listener_event_add(l);
}

void
all_listeners_event_del(struct lb_instance *instance)
{
    for (struct lb_listener *l = (struct lb_listener *)instance->listeners.next;
         &l->siblings != &instance->listeners;
         l = (struct lb_listener *)l->siblings.next)
        lb_listener_event_del(l);
}

bool
init_all_controls(struct lb_instance *instance, GError **error_r)
{
    for (struct lb_control_config *config = (struct lb_control_config *)instance->config->controls.next;
         &config->siblings != &instance->config->controls;
         config = (struct lb_control_config *)config->siblings.next) {
        struct lb_control *control = lb_control_new(instance, config, error_r);
        if (control == NULL)
            return false;

        list_add(&control->siblings, &instance->controls);
    }

    return true;
}

void
deinit_all_controls(struct lb_instance *instance)
{
    while (!list_empty(&instance->controls)) {
        struct lb_control *control =
            (struct lb_control *)instance->controls.next;
        list_remove(&control->siblings);
        lb_control_free(control);
    }
}
