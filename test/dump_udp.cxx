#include "udp_listener.hxx"
#include "pool.h"

#include <daemon/log.h>

#include <glib.h>
#include <event.h>

#include <stdio.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <signal.h>

static void
dump_udp_datagram(G_GNUC_UNUSED const void *data, size_t length,
                  G_GNUC_UNUSED const struct sockaddr *addr,
                  G_GNUC_UNUSED size_t addrlen,
                  int uid,
                  G_GNUC_UNUSED void *ctx)
{
    printf("packet: %zu uid=%d\n", length, uid);
}

static void
dump_udp_error(GError *error, G_GNUC_UNUSED void *ctx)
{
    g_printerr("%s\n", error->message);
    g_error_free(error);
}

static const struct udp_handler dump_udp_handler = {
    .datagram = dump_udp_datagram,
    .error = dump_udp_error,
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

    struct event_base *event_base = event_init();

    struct pool *pool = pool_new_libc(nullptr, "root");

    GError *error = nullptr;
    struct udp_listener *udp =
        udp_listener_port_new(pool, listen_host, 1234,
                              &dump_udp_handler, nullptr, &error);
    if (udp == nullptr) {
        g_printerr("%s\n", error->message);
        g_error_free(error);
        return 2;
    }

    if (mcast_group != nullptr) {
        struct in_addr addr = {
            .s_addr = inet_addr(mcast_group),
        };

        if (!udp_listener_join4(udp, &addr, &error)) {
            g_printerr("%s\n", error->message);
            g_error_free(error);
            return 2;
        }
    }

    event_dispatch();

    pool_commit();
    pool_unref(pool);
    pool_commit();
    pool_recycler_clear();

    event_base_free(event_base);

    return 0;
}
