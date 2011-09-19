/*
 * Fork a process and delegate open() to it.  The subprocess returns
 * the file descriptor over a unix socket.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_DELEGATE_CLIENT_H
#define BENG_DELEGATE_CLIENT_H

#include <glib.h>

#include "pool.h"

struct lease;
struct async_operation_ref;

struct delegate_handler {
    void (*success)(int fd, void *ctx);
    void (*error)(GError *error, void *ctx);
};

G_GNUC_CONST
static inline GQuark
delegate_client_quark(void)
{
    return g_quark_from_static_string("delegate_client");
}

/**
 * Opens a file with the delegate.
 *
 * @param fd the socket to the helper process
 */
void
delegate_open(int fd, const struct lease *lease, void *lease_ctx,
              pool_t pool, const char *path,
              const struct delegate_handler *handler, void *ctx,
              struct async_operation_ref *async_ref);

#endif
