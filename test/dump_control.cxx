#include "control_server.hxx"
#include "net/SocketAddress.hxx"

#include <inline/compiler.h>
#include <daemon/log.h>
#include <socket/resolver.h>
#include <socket/util.h>

#include <glib.h>
#include <event.h>

#include <stdio.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <signal.h>

class DumpControlHandler final : public ControlHandler {
public:
    void OnControlPacket(gcc_unused ControlServer &control_server,
                         enum beng_control_command command,
                         gcc_unused const void *payload, size_t payload_length,
                         gcc_unused SocketAddress address) override {
        printf("packet command=%u length=%zu\n", command, payload_length);
    }

    void OnControlError(GError *error) override {
        g_printerr("%s\n", error->message);
        g_error_free(error);
    }
};

int main(int argc, char **argv) {
    daemon_log_config.verbose = 5;

    if (argc > 3) {
        fprintf(stderr, "usage: dump-control [LISTEN:PORT [MCAST_GROUP]]\n");
        return 1;
    }

    const char *listen_host = argc >= 2 ? argv[1] : "*";
    const char *mcast_group = argc >= 3 ? argv[2] : NULL;

    signal(SIGPIPE, SIG_IGN);

    struct event_base *event_base = event_init();

    struct in_addr mcast_group_addr;
    if (mcast_group != NULL)
        mcast_group_addr.s_addr = inet_addr(mcast_group);

    DumpControlHandler handler;

    GError *error = NULL;
    ControlServer cs(handler);
    if (!cs.OpenPort(listen_host, 1234,
                     mcast_group != nullptr ? &mcast_group_addr : nullptr,
                     &error)) {
        g_printerr("%s\n", error->message);
        g_error_free(error);
        return 2;
    }

    event_dispatch();

    event_base_free(event_base);

    return 0;
}
