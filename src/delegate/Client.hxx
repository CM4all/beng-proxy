/*
 * Fork a process and delegate open() to it.  The subprocess returns
 * the file descriptor over a unix socket.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_DELEGATE_CLIENT_HXX
#define BENG_DELEGATE_CLIENT_HXX

#include <glib.h>

struct pool;
struct lease;
struct async_operation_ref;
class DelegateHandler;

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
              struct pool *pool, const char *path,
              DelegateHandler &handler,
              struct async_operation_ref *async_ref);

#endif
