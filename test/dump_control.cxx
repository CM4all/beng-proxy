#include "control_server.hxx"
#include "net/SocketAddress.hxx"
#include "event/Loop.hxx"
#include "system/SetupProcess.hxx"
#include "util/Error.hxx"
#include "util/PrintException.hxx"

#include <inline/compiler.h>
#include <daemon/log.h>
#include <socket/resolver.h>
#include <socket/util.h>

#include <stdio.h>
#include <sys/socket.h>
#include <arpa/inet.h>

class DumpControlHandler final : public ControlHandler {
public:
    void OnControlPacket(gcc_unused ControlServer &control_server,
                         enum beng_control_command command,
                         gcc_unused const void *payload, size_t payload_length,
                         gcc_unused SocketAddress address) override {
        printf("packet command=%u length=%zu\n", command, payload_length);
    }

    void OnControlError(Error &&error) override {
        fprintf(stderr, "%s\n", error.GetMessage());
    }
};

int main(int argc, char **argv)
try {
    daemon_log_config.verbose = 5;

    if (argc > 3) {
        fprintf(stderr, "usage: dump-control [LISTEN:PORT [MCAST_GROUP]]\n");
        return 1;
    }

    const char *listen_host = argc >= 2 ? argv[1] : "*";
    const char *mcast_group = argc >= 3 ? argv[2] : NULL;

    SetupProcess();

    EventLoop event_loop;

    struct in_addr mcast_group_addr;
    if (mcast_group != NULL)
        mcast_group_addr.s_addr = inet_addr(mcast_group);

    DumpControlHandler handler;

    Error error;
    ControlServer cs(handler);
    cs.OpenPort(listen_host, 1234,
                mcast_group != nullptr ? &mcast_group_addr : nullptr);

    event_loop.Dispatch();

    return 0;
} catch (const std::exception &e) {
    PrintException(e);
    return EXIT_FAILURE;
 }
