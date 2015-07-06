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

static void
dump_control_packet(enum beng_control_command command,
                    gcc_unused const void *payload, size_t payload_length,
                    gcc_unused SocketAddress address,
                    gcc_unused void *ctx)
{
    printf("packet command=%u length=%zu\n", command, payload_length);
}

static void
dump_control_error(GError *error, gcc_unused void *ctx)
{
    g_printerr("%s\n", error->message);
    g_error_free(error);
}

static const struct control_handler dump_control_handler = {
    .packet = dump_control_packet,
    .error = dump_control_error,
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

    GError *error = NULL;
    auto *cs =
        control_server_new_port(listen_host, 1234,
                                mcast_group != NULL ? &mcast_group_addr : NULL,
                                &dump_control_handler, NULL, &error);
    if (cs == NULL) {
        g_printerr("%s\n", error->message);
        g_error_free(error);
        return 2;
    }

    event_dispatch();

    event_base_free(event_base);

    return 0;
}
