/*
 * Launch "Local HTTP" child processes.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_LHTTP_LAUNCH_H
#define BENG_PROXY_LHTTP_LAUNCH_H

#include "child_socket.h"

#include <glib.h>

#include <stdbool.h>
#include <sys/types.h>

struct lhttp_address;

struct lhttp_process {
    pid_t pid;
    struct child_socket socket;
};

bool
lhttp_launch(struct lhttp_process *process,
             const struct lhttp_address *address,
             GError **error_r);

static inline void
lhttp_process_unlink_socket(struct lhttp_process *process)
{
    child_socket_unlink(&process->socket);
}

static inline const struct sockaddr *
lhttp_process_address(const struct lhttp_process *process)
{
    return child_socket_address(&process->socket);
}

static inline socklen_t
lhttp_process_address_length(const struct lhttp_process *process)
{
    return child_socket_address_length(&process->socket);
}

static inline int
lhttp_process_connect(const struct lhttp_process *process, GError **error_r)
{
    return child_socket_connect(&process->socket, error_r);
}

#endif
