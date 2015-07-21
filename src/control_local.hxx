/*
 * Control server on an implicitly configured local socket.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_CONTROL_LOCAL_HXX
#define BENG_PROXY_CONTROL_LOCAL_HXX

struct LocalControl;
struct ControlServer;
class ControlHandler;
class Error;

LocalControl *
control_local_new(const char *prefix, ControlHandler &handler);

void
control_local_free(LocalControl *cl);

bool
control_local_open(LocalControl *cl, Error &error_r);

ControlServer *
control_local_get(LocalControl *cl);

#endif
