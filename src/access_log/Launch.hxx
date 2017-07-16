/*
 * Launch logger child processes.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_LOG_LAUNCH_H
#define BENG_PROXY_LOG_LAUNCH_H

#include "net/UniqueSocketDescriptor.hxx"

#include <sys/types.h>

struct UidGid;

struct LogProcess {
    pid_t pid;
    UniqueSocketDescriptor fd;
};

LogProcess
log_launch(const char *program,
           const UidGid *uid_gid);

#endif
