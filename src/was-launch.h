/*
 * Launch WAS child processes.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_WAS_LAUNCH_H
#define BENG_PROXY_WAS_LAUNCH_H

#include <glib.h>
#include <stdbool.h>
#include <sys/types.h>

struct jail_params;

struct was_process {
    pid_t pid;
    int control_fd, input_fd, output_fd;
};

bool
was_launch(struct was_process *process,
           const char *executable_path,
           const char *const*args, unsigned n_args,
           const struct jail_params *jail,
           GError **error_r);

#endif
