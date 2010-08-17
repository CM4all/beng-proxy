/*
 * Launch WAS child processes.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_WAS_LAUNCH_H
#define BENG_PROXY_WAS_LAUNCH_H

#include <stdbool.h>
#include <sys/types.h>

struct was_process {
    pid_t pid;
    int control_fd, input_fd, output_fd;
};

bool
was_launch(struct was_process *process,
           const char *executable_path, const char *jail_path);

#endif
