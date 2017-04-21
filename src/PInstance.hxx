/*
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_P_INSTANCE_HXX
#define BENG_PROXY_P_INSTANCE_HXX

#include "RootPool.hxx"
#include "event/Loop.hxx"

/**
 * A base class for "Instance" structs which manages an #EventLoop and
 * a #RootPool.
 */
struct PInstance {
    EventLoop event_loop;

    RootPool root_pool;

    PInstance();
    ~PInstance();
};

#endif
