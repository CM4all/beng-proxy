/*
 * Fork a process and delegate open() to it.  The subprocess returns
 * the file descriptor over a unix socket.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_DELEGATE_CLIENT_H
#define BENG_DELEGATE_CLIENT_H

#include "pool.h"

struct lease;
struct async_operation_ref;

typedef void (*delegate_callback_t)(int fd, void *ctx);

/**
 * Opens a file with the delegate.
 *
 * @param fd the socket to the helper process
 */
void
delegate_open(int fd, const struct lease *lease, void *lease_ctx,
              pool_t pool, const char *path,
              delegate_callback_t callback, void *ctx,
              struct async_operation_ref *async_ref);

#endif
