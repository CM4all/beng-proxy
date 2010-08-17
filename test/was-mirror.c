#include "was-server.h"
#include "duplex.h"
#include "direct.h"

#include <daemon/log.h>

#include <stdio.h>
#include <stdlib.h>
#include <event.h>

struct instance {
    struct was_server *server;
};

static void
mirror_request(pool_t pool, http_method_t method, const char *uri,
               struct strmap *headers, istream_t body, void *ctx)
{
    struct instance *instance = ctx;

    (void)pool;
    (void)method;
    (void)uri;

    was_server_response(instance->server, HTTP_STATUS_OK, headers, body);
}

static void
mirror_free(void *ctx)
{
    (void)ctx;
}

static const struct was_server_handler handler = {
    .request = mirror_request,
    .free = mirror_free,
};

int main(int argc, char **argv) {
    (void)argc;
    (void)argv;

    daemon_log_config.verbose = 5;

    int in_fd = 0, out_fd = 1, control_fd = 3;

    direct_global_init();
    struct event_base *event_base = event_init();

    pool_t pool = pool_new_libc(NULL, "root");

    struct instance instance;
    instance.server = was_server_new(pool, control_fd, in_fd, out_fd,
                                     &handler, &instance);

    event_dispatch();

    was_server_free(instance.server);

    pool_unref(pool);
    pool_commit();
    pool_recycler_clear();

    event_base_free(event_base);
    direct_global_deinit();
}
