#include "control-server.h"

#include <daemon/log.h>
#include <socket/resolver.h>
#include <socket/util.h>

#include <glib.h>
#include <event.h>

#include <stdio.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

static void
dump_control_packet(enum beng_control_command command,
                    G_GNUC_UNUSED const void *payload, size_t payload_length,
                    G_GNUC_UNUSED void *ctx)
{
    printf("packet command=%u length=%zu\n", command, payload_length);
}

static const struct control_handler dump_control_handler = {
    .packet = dump_control_packet,
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

    pool_t pool = pool_new_libc(NULL, "root");

    struct in_addr mcast_group_addr;
    if (mcast_group != NULL)
        mcast_group_addr.s_addr = inet_addr(mcast_group);

    struct control_server *cs =
            control_server_new(pool, listen_host, 1234,
                               mcast_group != NULL ? &mcast_group_addr : NULL,
                               &dump_control_handler, NULL);
    if (cs == NULL)
        return 2;

    event_dispatch();

    pool_commit();
    pool_unref(pool);
    pool_commit();
    pool_recycler_clear();

    event_base_free(event_base);

    return 0;
}
