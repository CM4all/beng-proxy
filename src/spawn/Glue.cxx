/*
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "Glue.hxx"
#include "Client.hxx"
#include "Launch.hxx"
#include "Registry.hxx"
#include "system/Error.hxx"

#include <unistd.h>
#include <sys/socket.h>

SpawnServerClient *
StartSpawnServer(const SpawnConfig &config,
                 ChildProcessRegistry &child_process_registry,
                 std::function<void()> post_clone)
{
    int sv[2];
    if (socketpair(AF_LOCAL, SOCK_SEQPACKET|SOCK_CLOEXEC|SOCK_NONBLOCK,
                   0, sv) < 0)
        throw MakeErrno("socketpair() failed");

    const int close_fd = sv[1];

    pid_t pid;
    try {
        pid = LaunchSpawnServer(config, sv[0],
                                [close_fd, post_clone](){
                                    close(close_fd);
                                    post_clone();
                                });
    } catch (...) {
        close(sv[0]);
        close(sv[1]);
        throw;
    }

    child_process_registry.Add(pid, "spawn", nullptr);

    close(sv[0]);
    return new SpawnServerClient(child_process_registry.GetEventLoop(),
                                 config, sv[1]);
}
