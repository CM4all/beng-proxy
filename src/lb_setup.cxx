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
init_monitors(LbInstance &instance, const LbClusterConfig &cluster)
{
    if (cluster.monitor == NULL)
        return;

    for (const auto &member : cluster.members)
        instance.monitors.Add(*member.node, member.port, *cluster.monitor);
}

static void
init_monitors(LbInstance &instance, const LbBranchConfig &cluster);

static void
init_monitors(LbInstance &instance, const LbGoto &g)
{
    if (g.cluster != nullptr)
        init_monitors(instance, *g.cluster);
    else
        init_monitors(instance, *g.branch);
}

static void
init_monitors(LbInstance &instance, const LbGotoIfConfig &gif)
{
    init_monitors(instance, gif.destination);
}

static void
init_monitors(LbInstance &instance, const LbBranchConfig &cluster)
{
    init_monitors(instance, cluster.fallback);

    for (const auto &i : cluster.conditions)
        init_monitors(instance, i);
}

bool
init_all_listeners(LbInstance &instance, Error &error)
{
    auto &listeners = instance.listeners;

    for (const auto &config : instance.config->listeners) {
        listeners.emplace_front(instance, config);
        auto &listener = listeners.front();
        if (!listener.Setup(error))
            return false;

        init_monitors(instance, config.destination);
    }

    return true;
}

void
deinit_all_listeners(LbInstance *instance)
{
    instance->listeners.clear();
}

void
all_listeners_event_add(LbInstance *instance)
{
    for (auto &listener : instance->listeners)
        listener.AddEvent();
}

void
all_listeners_event_del(LbInstance *instance)
{
    for (auto &listener : instance->listeners)
        listener.RemoveEvent();
}

unsigned
LbInstance::FlushSSLSessionCache(long tm)
{
    unsigned n = 0;
    for (auto &listener : listeners)
        n += listener.FlushSSLSessionCache(tm);
    return n;
}

void
init_all_controls(LbInstance *instance)
{
    for (const auto &config : instance->config->controls) {
        instance->controls.emplace_front(*instance);
        auto &control = instance->controls.front();
        control.Open(config);

        if (instance->cmdline.watchdog)
            /* disable the control channel in the "master" process, it
               shall only apply to the one worker */
            control.Disable();
    }
}

void
deinit_all_controls(LbInstance *instance)
{
    instance->controls.clear();
}

void
enable_all_controls(LbInstance *instance)
{
    for (auto &control : instance->controls)
        control.Enable();
}
