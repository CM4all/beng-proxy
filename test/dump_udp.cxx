#include "udp_listener.hxx"
#include "pool.hxx"
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

int main(int argc, char **argv) {
    daemon_log_config.verbose = 5;

    if (argc > 3) {
        fprintf(stderr, "usage: dump-udp [LISTEN:PORT [MCAST_GROUP]]\n");
        return 1;
    }

    const char *listen_host = argc >= 2 ? argv[1] : "*";
    const char *mcast_group = argc >= 3 ? argv[2] : nullptr;

    signal(SIGPIPE, SIG_IGN);

    EventBase event_base;

    struct pool *pool = pool_new_libc(nullptr, "root");

    DumpUdpHandler handler;

    Error error;
    auto *udp = udp_listener_port_new(listen_host, 1234, handler, error);
    if (udp == nullptr) {
        fprintf(stderr, "%s\n", error.GetMessage());
        return 2;
    }

    if (mcast_group != nullptr) {
        struct in_addr addr = {
            .s_addr = inet_addr(mcast_group),
        };

        if (!udp_listener_join4(udp, &addr, error)) {
            fprintf(stderr, "%s\n", error.GetMessage());
            return 2;
        }
    }

    event_base.Dispatch();

    pool_commit();
    pool_unref(pool);
    pool_commit();
    pool_recycler_clear();

    return 0;
}
