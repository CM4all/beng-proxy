/*
 * Launch WAS child processes.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "was_launch.hxx"
#include "system/fd_util.h"
#include "system/fd-util.h"
#include "spawn/Interface.hxx"
#include "spawn/Prepared.hxx"
#include "spawn/ChildOptions.hxx"
#include "gerrno.h"
#include "util/ConstBuffer.hxx"

#include <daemon/log.h>
#include <inline/compiler.h>

#include <sys/socket.h>
#include <unistd.h>

void
WasProcess::Close()
{
    if (control_fd >= 0) {
        close(control_fd);
        control_fd = -1;
    }

    if (input_fd >= 0) {
        close(input_fd);
        input_fd = -1;
    }

    if (output_fd >= 0) {
        close(output_fd);
        output_fd = -1;
    }
}

bool
was_launch(SpawnService &spawn_service,
           WasProcess *process,
           const char *name,
           const char *executable_path,
           ConstBuffer<const char *> args,
           const ChildOptions &options,
           ExitListener *listener,
           GError **error_r)
{
    int control_fds[2], input_fds[2], output_fds[2];

    PreparedChildProcess p;

    if (socketpair_cloexec(AF_UNIX, SOCK_STREAM, 0, control_fds) < 0) {
        set_error_errno_msg(error_r, "failed to create socket pair");
        return false;
    }

    process->control_fd = control_fds[0];
    p.control_fd = control_fds[1];

    if (pipe_cloexec(input_fds) < 0) {
        set_error_errno_msg(error_r, "failed to create first pipe");
        return false;
    }

    fd_set_nonblock(input_fds[0], true);
    process->input_fd = input_fds[0];
    p.stdout_fd = input_fds[1];

    if (pipe_cloexec(output_fds) < 0) {
        set_error_errno_msg(error_r, "failed to create second pipe");
        return false;
    }

    p.stdin_fd = output_fds[0];
    fd_set_nonblock(output_fds[1], true);
    process->output_fd = output_fds[1];

    p.Append(executable_path);
    for (auto i : args)
        p.Append(i);

    if (!options.CopyTo(p, true, nullptr, error_r))
        return false;

    int pid = spawn_service.SpawnChildProcess(name, std::move(p), listener,
                                              error_r);
    if (pid < 0)
        return false;

    process->pid = pid;
    return true;
}
