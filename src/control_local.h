/*
 * Control server on an implicitly configured local socket.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_CONTROL_LOCAL_H
#define BENG_PROXY_CONTROL_LOCAL_H

#include <glib.h>

#include <stdbool.h>

struct pool;
struct control_local;
struct control_handler;

struct control_local *
control_local_new(struct pool *pool, const char *prefix,
                  const struct control_handler *handler, void *ctx);

void
control_local_free(struct control_local *cl);

bool
control_local_open(struct control_local *cl, GError **error_r);

struct control_server *
control_local_get(struct control_local *cl);

#endif
