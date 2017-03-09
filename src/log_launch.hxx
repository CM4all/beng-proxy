/*
 * Launch logger child processes.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_LOG_LAUNCH_H
#define BENG_PROXY_LOG_LAUNCH_H

#include <sys/types.h>

struct daemon_user;

struct log_process {
    pid_t pid;
    int fd;
};

bool
log_launch(struct log_process *process, const char *program,
           const struct daemon_user *user);

#endif
