#include "http-client.h"
#include "http-response.h"
#include "duplex.h"

#include <inline/compiler.h>

#include <sys/types.h>
#include <sys/socket.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <event.h>

static http_client_connection_t
connect_mirror(pool_t pool,
               const struct http_client_connection_handler *handler,
               void *ctx)
{
    int ret, sv[2];
    pid_t pid;

    ret = socketpair(AF_UNIX, SOCK_STREAM, 0, sv);
    if (ret < 0) {
        perror("socketpair() failed");
        exit(EXIT_FAILURE);
    }

    pid = fork();
    if (pid < 0) {
        perror("fork() failed");
        exit(EXIT_FAILURE);
    }

    if (pid == 0) {
        dup2(sv[1], 0);
        dup2(sv[1], 1);
        close(sv[0]);
        close(sv[1]);
        execl("./test/t-http-server-mirror", "t-http-server-mirror", NULL);
        perror("exec() failed");
        exit(EXIT_FAILURE);
    }

    close(sv[1]);

    return http_client_connection_new(pool, sv[0], handler, ctx);
}

struct context {
    http_client_connection_t client;
    int idle, aborted;
    http_status_t status;
};

static void
my_connection_idle(void *ctx)
{
    struct context *c = ctx;

    c->idle = 1;
}

static void
my_connection_free(void *ctx __attr_unused)
{
    struct context *c = ctx;

    c->client = NULL;
}

static const struct http_client_connection_handler my_connection_handler = {
    .idle = my_connection_idle,
    .free = my_connection_free,
};

static void
my_response(http_status_t status, strmap_t headers __attr_unused,
            istream_t body __attr_unused,
            void *ctx)
{
    struct context *c = ctx;

    c->status = status;
}

static void
my_response_abort(void *ctx)
{
    struct context *c = ctx;

    c->aborted = 1;
}

static const struct http_response_handler my_response_handler = {
    .response = my_response,
    .abort = my_response_abort,
};

static void
test_empty(pool_t pool)
{
    struct context c;

    memset(&c, 0, sizeof(c));
    c.client = connect_mirror(pool, &my_connection_handler, &c);
    http_client_request(c.client, HTTP_METHOD_GET, "/foo", NULL, NULL,
                        &my_response_handler, &c);

    event_dispatch();

    assert(c.client == NULL);
    assert(c.status == HTTP_STATUS_NO_CONTENT);
}

static void
test_early_close(pool_t pool)
{
    struct context c;

    memset(&c, 0, sizeof(c));
    c.client = connect_mirror(pool, &my_connection_handler, &c);
    http_client_connection_close(c.client);

    assert(c.client == NULL);
}

static void
run_test(pool_t pool, void (*test)(pool_t pool)) {
    pool = pool_new_linear(pool, "test", 16384);
    test(pool);
    pool_unref(pool);
    pool_commit();
}

int main(int argc, char **argv) {
    pool_t pool;

    (void)argc;
    (void)argv;

    event_init();

    pool = pool_new_libc(NULL, "root");

    run_test(pool, test_empty);
    run_test(pool, test_early_close);

    pool_unref(pool);
    pool_commit();
    pool_recycler_clear();
}
