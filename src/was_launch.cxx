/*
 * Launch WAS child processes.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "was_launch.hxx"
#include "fd_util.h"
#include "fd-util.h"
#include "exec.hxx"
#include "child_options.hxx"
#include "sigutil.h"
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

struct was_run_args {
    sigset_t signals;

    const struct child_options *options;

    int control_fd, input_fd, output_fd;

    const char *executable_path;
    ConstBuffer<const char *> args;
    ConstBuffer<const char *> env;
};

gcc_noreturn
static int
was_run(void *ctx)
{
    struct was_run_args *args = (struct was_run_args *)ctx;

    install_default_signal_handlers();
    leave_signal_section(&args->signals);

    args->options->SetupStderr();
    namespace_options_setup(&args->options->ns);
    rlimit_options_apply(&args->options->rlimits);

    dup2(args->input_fd, 0);
    dup2(args->output_fd, 1);
    /* fd2 is retained */
    dup2(args->control_fd, 3);

    Exec exec;
    jail_wrapper_insert(exec, &args->options->jail, nullptr);
    exec.Append(args->executable_path);
    for (auto i : args->args)
        exec.Append(i);
    for (auto i : args->env)
        exec.PutEnv(i);

    exec.DoExec();
}

bool
was_launch(struct was_process *process,
           const char *executable_path,
           ConstBuffer<const char *> args,
           ConstBuffer<const char *> env,
           const struct child_options *options,
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
        .options = options,
        .control_fd = control_fds[1],
        .output_fd = input_fds[1],
        .input_fd = output_fds[0],
        .executable_path = executable_path,
        .args = args,
        .env = env,
    };

    int clone_flags = SIGCHLD;
    clone_flags = namespace_options_clone_flags(&options->ns, clone_flags);

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
