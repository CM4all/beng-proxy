/*
 * Set up load balancer objects.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "lb_setup.h"
#include "lb_config.h"
#include "lb_instance.h"
#include "lb_listener.h"

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
