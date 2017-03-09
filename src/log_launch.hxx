/*
 * Launch logger child processes.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_LOG_LAUNCH_H
#define BENG_PROXY_LOG_LAUNCH_H

#include "io/UniqueFileDescriptor.hxx"

#include <sys/types.h>

struct daemon_user;

struct LogProcess {
    pid_t pid;
    UniqueFileDescriptor fd;
};

LogProcess
log_launch(const char *program,
           const struct daemon_user *user);

#endif
