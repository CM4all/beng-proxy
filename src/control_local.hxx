/*
 * Control server on an implicitly configured local socket.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_CONTROL_LOCAL_HXX
#define BENG_PROXY_CONTROL_LOCAL_HXX

struct LocalControl;
class ControlServer;
class ControlHandler;
class EventLoop;
class Error;

LocalControl *
control_local_new(const char *prefix, ControlHandler &handler);

void
control_local_free(LocalControl *cl);

void
control_local_open(LocalControl *cl, EventLoop &event_loop);

ControlServer *
control_local_get(LocalControl *cl);

#endif
