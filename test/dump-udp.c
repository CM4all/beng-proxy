#include "udp-listener.h"

#include <daemon/log.h>

#include <glib.h>
#include <event.h>

#include <stdio.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

static void
dump_udp_callback(G_GNUC_UNUSED const void *data, size_t length,
                  G_GNUC_UNUSED const struct sockaddr *addr,
                  G_GNUC_UNUSED size_t addrlen,
                  G_GNUC_UNUSED void *ctx)
{
    printf("packet: %zu\n", length);
}

int main(int argc, char **argv) {
    daemon_log_config.verbose = 5;

    if (argc > 3) {
        fprintf(stderr, "usage: dump-udp [LISTEN:PORT [MCAST_GROUP]]\n");
        return 1;
    }

    const char *listen_host = argc >= 2 ? argv[1] : "*";
    const char *mcast_group = argc >= 3 ? argv[2] : NULL;

    signal(SIGPIPE, SIG_IGN);

    struct event_base *event_base = event_init();

    pool_t pool = pool_new_libc(NULL, "root");

    struct udp_listener *udp =
        udp_listener_port_new(pool, listen_host, 1234,
                              dump_udp_callback, NULL);
    if (udp == NULL)
        return 2;

    if (mcast_group != NULL) {
        struct in_addr addr = {
            .s_addr = inet_addr(mcast_group),
        };
        if (!udp_listener_join4(udp, &addr))
            return 2;
    }

    event_dispatch();

    pool_commit();
    pool_unref(pool);
    pool_commit();
    pool_recycler_clear();

    event_base_free(event_base);

    return 0;
}
