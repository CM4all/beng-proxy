/*
 * Set up load balancer objects.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "lb_setup.hxx"
#include "lb_config.hxx"
#include "lb_instance.hxx"
#include "lb_listener.hxx"
#include "lb_hmonitor.hxx"
#include "lb_control.hxx"

static void
init_monitors(const lb_cluster_config &cluster)
{
    if (cluster.monitor == NULL)
        return;

    for (const auto &member : cluster.members)
        lb_hmonitor_add(member.node, member.port, cluster.monitor);
}

static void
init_monitors(const lb_branch_config &cluster);

static void
init_monitors(const lb_goto &g)
{
    if (g.cluster != nullptr)
        init_monitors(*g.cluster);
    else
        init_monitors(*g.branch);
}

static void
init_monitors(const lb_goto_if_config &gif)
{
    init_monitors(gif.destination);
}

static void
init_monitors(const lb_branch_config &cluster)
{
    init_monitors(cluster.fallback);

    for (const auto &i : cluster.conditions)
        init_monitors(i);
}

bool
init_all_listeners(struct lb_instance &instance, GError **error_r)
{
    bool success = true;

    for (const auto &config : instance.config->listeners) {
        struct lb_listener *listener = lb_listener_new(instance, config,
                                                       error_r);
        if (listener == NULL)
            return false;

        list_add(&listener->siblings, &instance.listeners);

        init_monitors(config.destination);
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
        delete listener;
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
    for (const auto &config : instance->config->controls) {
        struct lb_control *control = lb_control_new(instance, &config, error_r);
        if (control == NULL)
            return false;

        if (instance->cmdline.num_workers > 0)
            /* disable the control channel in the "master" process, it
               shall only apply to the one worker */
            lb_control_disable(control);

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

void
enable_all_controls(struct lb_instance *instance)
{
    for (struct lb_control *control = (struct lb_control *)instance->controls.next;
         &control->siblings != &instance->controls;
         control = (struct lb_control *)control->siblings.next)
        lb_control_enable(control);
}
