/*
 * Launch WAS child processes.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_WAS_LAUNCH_HXX
#define BENG_PROXY_WAS_LAUNCH_HXX

#include "glibfwd.hxx"

#include <sys/types.h>

struct ChildOptions;
template<typename T> struct ConstBuffer;

struct was_process {
    pid_t pid;
    int control_fd, input_fd, output_fd;
};

bool
was_launch(struct was_process *process,
           const char *executable_path,
           ConstBuffer<const char *> args,
           ConstBuffer<const char *> env,
           const ChildOptions *options,
           GError **error_r);

#endif
