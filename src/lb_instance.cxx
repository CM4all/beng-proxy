/*
 * Global declarations.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "lb_instance.hxx"
#include "lb_control.hxx"
#include "lb_listener.hxx"

#include <assert.h>

lb_instance::lb_instance()
        :shutdown_listener(ShutdownCallback, this) {}

lb_instance::~lb_instance()
{
    assert(n_tcp_connections == 0);
}
