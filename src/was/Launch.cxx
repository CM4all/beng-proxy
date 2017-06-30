/*
 * Launch WAS child processes.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "Launch.hxx"
#include "spawn/Interface.hxx"
#include "spawn/Prepared.hxx"
#include "spawn/ChildOptions.hxx"
#include "system/Error.hxx"
#include "util/ConstBuffer.hxx"

#include "util/Compiler.h"

#include <sys/socket.h>
#include <unistd.h>

WasProcess
was_launch(SpawnService &spawn_service,
           const char *name,
           const char *executable_path,
           ConstBuffer<const char *> args,
           const ChildOptions &options,
           ExitListener *listener)
{
    WasProcess process;

    PreparedChildProcess p;

    UniqueFileDescriptor control_fd;
    if (!UniqueFileDescriptor::CreateSocketPair(AF_LOCAL, SOCK_STREAM, 0,
                                                control_fd, process.control))
        throw MakeErrno("Failed to create socket pair");

    p.SetControl(std::move(control_fd));

    UniqueFileDescriptor input_r, input_w;
    if (!UniqueFileDescriptor::CreatePipe(input_r, input_w))
        throw MakeErrno("Failed to create first pipe");

    input_r.SetNonBlocking();
    process.input = std::move(input_r);
    p.SetStdout(std::move(input_w));

    UniqueFileDescriptor output_r, output_w;
    if (!UniqueFileDescriptor::CreatePipe(output_r, output_w))
        throw MakeErrno("Failed to create second pipe");

    p.SetStdin(std::move(output_r));
    output_w.SetNonBlocking();
    process.output = std::move(output_w);

    p.Append(executable_path);
    for (auto i : args)
        p.Append(i);

    options.CopyTo(p, true, nullptr);
    process.pid = spawn_service.SpawnChildProcess(name, std::move(p),
                                                  listener);
    return process;
}
