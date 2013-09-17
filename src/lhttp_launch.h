/*
 * Launch "Local HTTP" child processes.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_LHTTP_LAUNCH_H
#define BENG_PROXY_LHTTP_LAUNCH_H

#include <glib.h>

#include <stdbool.h>
#include <sys/types.h>
#include <sys/un.h>

struct lhttp_address;

struct lhttp_process {
    pid_t pid;
    struct sockaddr_un address;
};

bool
lhttp_launch(struct lhttp_process *process,
             const struct lhttp_address *address,
             GError **error_r);

#endif
