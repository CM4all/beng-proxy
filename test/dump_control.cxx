#include "control_server.hxx"
#include "net/SocketAddress.hxx"
#include "net/IPv4Address.hxx"
#include "event/Loop.hxx"
#include "system/SetupProcess.hxx"
#include "util/PrintException.hxx"

#include "util/Compiler.h"
#include <daemon/log.h>

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

    void OnControlError(std::exception_ptr ep) override {
        PrintException(ep);
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

    ControlServer cs(handler);
    cs.OpenPort(event_loop, listen_host, 1234,
                mcast_group != nullptr ? SocketAddress(IPv4Address(mcast_group_addr, 0)) : nullptr);

    event_loop.Dispatch();

    return 0;
} catch (const std::exception &e) {
    PrintException(e);
    return EXIT_FAILURE;
 }
