#include "was/was_server.hxx"
#include "direct.hxx"
#include "pool.hxx"
#include "fb_pool.hxx"
#include "event/Event.hxx"

#include <daemon/log.h>

#include <stdio.h>
#include <stdlib.h>

struct Instance final : WasServerHandler {
    WasServer *server;

    void OnWasRequest(gcc_unused struct pool &pool,
                      gcc_unused http_method_t method,
                      gcc_unused const char *uri, struct strmap &&headers,
                      Istream *body) override {
        was_server_response(server, HTTP_STATUS_OK, &headers, body);
    }

    void OnWasClosed() override {}
};

int main(int argc, char **argv) {
    (void)argc;
    (void)argv;

    daemon_log_config.verbose = 5;

    int in_fd = 0, out_fd = 1, control_fd = 3;

    direct_global_init();
    fb_pool_init(false);
    EventBase event_base;

    struct pool *pool = pool_new_libc(nullptr, "root");

    Instance instance;
    instance.server = was_server_new(pool, control_fd, in_fd, out_fd,
                                     instance);

    event_base.Dispatch();

    was_server_free(instance.server);

    pool_unref(pool);
    pool_commit();
    pool_recycler_clear();

    fb_pool_deinit();
    direct_global_deinit();
}
