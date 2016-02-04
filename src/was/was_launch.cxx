/*
 * Launch WAS child processes.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "was_launch.hxx"
#include "system/fd_util.h"
#include "system/fd-util.h"
#include "system/sigutil.h"
#include "spawn/Spawn.hxx"
#include "spawn/Prepared.hxx"
#include "spawn/ChildOptions.hxx"
#include "gerrno.h"
#include "util/ConstBuffer.hxx"

#include <daemon/log.h>
#include <inline/compiler.h>

#include <sys/socket.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>

#ifdef __linux
#include <sched.h>
#endif

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

struct was_run_args {
    sigset_t signals;

    const ChildOptions *options;

    int control_fd, input_fd, output_fd;

    const char *executable_path;
    ConstBuffer<const char *> args;
};

gcc_noreturn
static int
was_run(void *ctx)
{
    struct was_run_args *args = (struct was_run_args *)ctx;

    install_default_signal_handlers();
    leave_signal_section(&args->signals);

    args->options->Apply();

    PreparedChildProcess exec;
    exec.stdin_fd = args->input_fd;
    exec.stdout_fd = args->output_fd;
    /* fd2 is retained */
    exec.control_fd = args->control_fd;

    exec.Append(args->executable_path);
    for (auto i : args->args)
        exec.Append(i);
    for (auto i : args->options->env)
        exec.PutEnv(i);

    args->options->jail.InsertWrapper(exec, nullptr);

    Exec(std::move(exec));
}

bool
was_launch(WasProcess *process,
           const char *executable_path,
           ConstBuffer<const char *> args,
           const ChildOptions &options,
           GError **error_r)
{
    int control_fds[2], input_fds[2], output_fds[2];

    if (socketpair_cloexec(AF_UNIX, SOCK_STREAM, 0, control_fds) < 0) {
        set_error_errno_msg(error_r, "failed to create socket pair");
        return false;
    }

    if (pipe_cloexec(input_fds) < 0) {
        set_error_errno_msg(error_r, "failed to create first pipe");
        close(control_fds[0]);
        close(control_fds[1]);
        return false;
    }

    if (pipe_cloexec(output_fds) < 0) {
        set_error_errno_msg(error_r, "failed to create second pipe");
        close(control_fds[0]);
        close(control_fds[1]);
        close(input_fds[0]);
        close(input_fds[1]);
        return false;
    }

    struct was_run_args run_args = {
        .signals = sigset_t(),
        .options = &options,
        .control_fd = control_fds[1],
        .input_fd = output_fds[0],
        .output_fd = input_fds[1],
        .executable_path = executable_path,
        .args = args,
    };

    int clone_flags = SIGCHLD;
    clone_flags = options.ns.GetCloneFlags(clone_flags);

    /* avoid race condition due to libevent signal handler in child
       process */
    enter_signal_section(&run_args.signals);

    char stack[8192];

    long pid = clone(was_run, stack + sizeof(stack),
                     clone_flags, &run_args);
    if (pid < 0) {
        leave_signal_section(&run_args.signals);

        set_error_errno_msg(error_r, "clone() failed");
        close(control_fds[0]);
        close(control_fds[1]);
        close(input_fds[0]);
        close(input_fds[1]);
        close(output_fds[0]);
        close(output_fds[1]);
        return false;
    }

    leave_signal_section(&run_args.signals);

    close(control_fds[1]);
    close(input_fds[1]);
    close(output_fds[0]);

    fd_set_nonblock(input_fds[0], true);
    fd_set_nonblock(output_fds[1], true);

    process->pid = pid;
    process->control_fd = control_fds[0];
    process->input_fd = input_fds[0];
    process->output_fd = output_fds[1];
    return true;
}
