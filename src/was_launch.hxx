/*
 * Launch WAS child processes.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_WAS_LAUNCH_HXX
#define BENG_PROXY_WAS_LAUNCH_HXX

#include <glib.h>
#include <stdbool.h>
#include <sys/types.h>

struct child_options;

struct was_process {
    pid_t pid;
    int control_fd, input_fd, output_fd;
};

bool
was_launch(struct was_process *process,
           const char *executable_path,
           const char *const*args, unsigned n_args,
           const struct child_options *options,
           GError **error_r);

#endif
