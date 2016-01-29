#include "udp_listener.hxx"
#include "net/SocketAddress.hxx"
#include "event/Base.hxx"
#include "util/Error.hxx"

#include <daemon/log.h>

#include <stdio.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <signal.h>

class DumpUdpHandler final : public UdpHandler {
public:
    /* virtual methods from class UdpHandler */
    void OnUdpDatagram(gcc_unused const void *data, size_t length,
                       gcc_unused SocketAddress address, int uid) override {
        printf("packet: %zu uid=%d\n", length, uid);
    }

    void OnUdpError(Error &&error) override {
        fprintf(stderr, "%s\n", error.GetMessage());
    }
};

static void
PrintException(const std::exception &e)
{
    fprintf(stderr, "%s\n", e.what());
    try {
        std::rethrow_if_nested(e);
    } catch (const std::exception &nested) {
        PrintException(nested);
    } catch (...) {
        fprintf(stderr, "Unrecognized nested exception\n");
    }
}

int main(int argc, char **argv)
try {
    daemon_log_config.verbose = 5;

    if (argc > 3) {
        fprintf(stderr, "usage: dump-udp [LISTEN:PORT [MCAST_GROUP]]\n");
        return 1;
    }

    const char *listen_host = argc >= 2 ? argv[1] : "*";
    const char *mcast_group = argc >= 3 ? argv[2] : nullptr;

    signal(SIGPIPE, SIG_IGN);

    EventBase event_base;

    DumpUdpHandler handler;

    auto *udp = udp_listener_port_new(listen_host, 1234, handler);

    if (mcast_group != nullptr) {
        struct in_addr addr = {
            .s_addr = inet_addr(mcast_group),
        };

        udp_listener_join4(udp, &addr);
    }

    event_base.Dispatch();

    return 0;
} catch (const std::exception &e) {
    PrintException(e);
    return EXIT_FAILURE;
}
