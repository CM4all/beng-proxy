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
#include "GException.hxx"
#include "util/ConstBuffer.hxx"

#include <daemon/log.h>
#include <inline/compiler.h>

#include <sys/socket.h>
#include <unistd.h>

WasProcess
was_launch(SpawnService &spawn_service,
           const char *name,
           const char *executable_path,
           ConstBuffer<const char *> args,
           const ChildOptions &options,
           ExitListener *listener,
           GError **error_r)
{
    WasProcess process;

    int control_fds[2];

    PreparedChildProcess p;

    if (socketpair_cloexec(AF_UNIX, SOCK_STREAM, 0, control_fds) < 0) {
        set_error_errno_msg(error_r, "failed to create socket pair");
        return process;
    }

    process.control = UniqueFileDescriptor(FileDescriptor(control_fds[0]));
    p.SetControl(control_fds[1]);

    UniqueFileDescriptor input_r, input_w;
    if (!UniqueFileDescriptor::CreatePipe(input_r, input_w)) {
        set_error_errno_msg(error_r, "failed to create first pipe");
        return process;
    }

    input_r.SetNonBlocking();
    process.input = std::move(input_r);
    p.SetStdout(std::move(input_w));

    UniqueFileDescriptor output_r, output_w;
    if (!UniqueFileDescriptor::CreatePipe(output_r, output_w)) {
        set_error_errno_msg(error_r, "failed to create second pipe");
        return process;
    }

    p.SetStdin(std::move(output_r));
    output_w.SetNonBlocking();
    process.output = std::move(output_w);

    p.Append(executable_path);
    for (auto i : args)
        p.Append(i);

    try {
        options.CopyTo(p, true, nullptr);
    } catch (const std::runtime_error &e) {
        SetGError(error_r, e);
        return process;
    }

    try {
        process.pid = spawn_service.SpawnChildProcess(name, std::move(p),
                                                      listener);
    } catch (const std::runtime_error &e) {
        process.pid = -1;
        SetGError(error_r, e);
    }

    return process;
}
