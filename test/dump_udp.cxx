#include "system/SetupProcess.hxx"
#include "net/UdpListener.hxx"
#include "net/UdpHandler.hxx"
#include "net/SocketAddress.hxx"
#include "event/Loop.hxx"
#include "util/Error.hxx"
#include "util/PrintException.hxx"

#include <daemon/log.h>

#include <stdio.h>
#include <sys/socket.h>
#include <arpa/inet.h>

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

    auto *udp = udp_listener_port_new(event_loop, listen_host, 1234, handler);

    if (mcast_group != nullptr) {
        struct in_addr addr = {
            .s_addr = inet_addr(mcast_group),
        };

        udp->Join4(&addr);
    }

    event_loop.Dispatch();

    return 0;
} catch (const std::exception &e) {
    PrintException(e);
    return EXIT_FAILURE;
}
