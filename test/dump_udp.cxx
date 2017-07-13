#include "system/SetupProcess.hxx"
#include "net/UdpListener.hxx"
#include "net/UdpHandler.hxx"
#include "net/AllocatedSocketAddress.hxx"
#include "net/Parser.hxx"
#include "event/Loop.hxx"
#include "util/PrintException.hxx"

#include <daemon/log.h>

#include <stdio.h>

class DumpUdpHandler final : public UdpHandler {
public:
    /* virtual methods from class UdpHandler */
    void OnUdpDatagram(gcc_unused const void *data, size_t length,
                       gcc_unused SocketAddress address, int uid) override {
        printf("packet: %zu uid=%d\n", length, uid);
    }

    void OnUdpError(std::exception_ptr ep) override {
        PrintException(ep);
    }
};

int main(int argc, char **argv)
try {
    daemon_log_config.verbose = 5;

    if (argc > 3) {
        fprintf(stderr, "usage: dump-udp [LISTEN:PORT [MCAST_GROUP]]\n");
        return 1;
    }

    const char *listen_host = argc >= 2 ? argv[1] : "*";
    const char *mcast_group = argc >= 3 ? argv[2] : nullptr;

    SetupProcess();

    EventLoop event_loop;

    DumpUdpHandler handler;

    const auto bind_address = ParseSocketAddress(listen_host, 1234, true);
    const auto group_address = mcast_group != nullptr
        ? ParseSocketAddress(mcast_group, 0, false)
        : AllocatedSocketAddress();

    auto *udp = udp_listener_new(event_loop, bind_address,
                                 group_address, handler);

    event_loop.Dispatch();

    delete udp;
    return 0;
} catch (const std::exception &e) {
    PrintException(e);
    return EXIT_FAILURE;
}
