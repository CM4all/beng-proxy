/*
 * Control server on an implicitly configured local socket.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_CONTROL_LOCAL_HXX
#define BENG_PROXY_CONTROL_LOCAL_HXX

#include "glibfwd.hxx"

struct control_local;
struct control_handler;
struct ControlServer;

struct control_local *
control_local_new(const char *prefix,
                  const struct control_handler *handler, void *ctx);

void
control_local_free(struct control_local *cl);

bool
control_local_open(struct control_local *cl, GError **error_r);

ControlServer *
control_local_get(struct control_local *cl);

#endif
