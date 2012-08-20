/*
 * Monitor which expects a string on a TCP connection.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "lb_expect_monitor.h"
#include "lb_monitor.h"
#include "lb_config.h"
#include "client-socket.h"
#include "pool.h"
#include "async.h"

#include <event.h>
#include <unistd.h>
#include <sys/socket.h>
#include <string.h>
#include <errno.h>

struct expect_monitor_context {
    struct pool *pool;
    const struct lb_monitor_config *config;

    int fd;

    struct event event;

    const struct lb_monitor_handler *handler;
    void *handler_ctx;

    struct async_operation_ref *async_ref;
    struct async_operation async_operation;
};

static bool
check_expectation(char *received, size_t received_length,
                  const char *expect)
{
    return g_strrstr_len(received, received_length, expect) != NULL;
}

/*
 * async operation
 *
 */

static struct expect_monitor_context *
async_to_expect_monitor(struct async_operation *ao)
{
    return (struct expect_monitor_context *)(((char*)ao) - offsetof(struct expect_monitor_context, async_operation));
}

static void
expect_monitor_request_abort(struct async_operation *ao)
{
    struct expect_monitor_context *expect = async_to_expect_monitor(ao);

    event_del(&expect->event);
    close(expect->fd);
    pool_unref(expect->pool);
}

static const struct async_operation_class expect_monitor_async_operation = {
    .abort = expect_monitor_request_abort,
};

/*
 * libevent callback
 *
 */

static void
expect_monitor_event_callback(G_GNUC_UNUSED int fd, short event, void *ctx)
{
    struct expect_monitor_context *expect = ctx;

    async_operation_finished(&expect->async_operation);

    if (event & EV_TIMEOUT) {
        close(expect->fd);
        expect->handler->timeout(expect->handler_ctx);
    } else {
        char buffer[1024];

        ssize_t nbytes = recv(expect->fd, buffer, sizeof(buffer),
                              MSG_DONTWAIT);
        if (nbytes < 0) {
            GError *error = g_error_new_literal(g_file_error_quark(), errno,
                                                strerror(errno));
            close(fd);
            expect->handler->error(error, expect->handler_ctx);
        } else if (expect->config->fade_expect != NULL &&
                   check_expectation(buffer, nbytes,
                                     expect->config->fade_expect)) {
            close(fd);
            expect->handler->fade(expect->handler_ctx);
        } else if (expect->config->expect == NULL ||
                   check_expectation(buffer, nbytes, expect->config->expect)) {
            close(fd);
            expect->handler->success(expect->handler_ctx);
        } else {
            close(fd);
            GError *error = g_error_new_literal(g_file_error_quark(), 0,
                                                "Expectation failed");
            expect->handler->error(error, expect->handler_ctx);
        }
    }

    pool_unref(expect->pool);
    pool_commit();
}

/*
 * client_socket handler
 *
 */

static void
expect_monitor_success(int fd, void *ctx)
{
    struct expect_monitor_context *expect = ctx;

    if (expect->config->send != NULL) {
        ssize_t nbytes = send(fd, expect->config->send,
                              strlen(expect->config->send),
                              MSG_DONTWAIT);
        if (nbytes < 0) {
            GError *error = g_error_new_literal(g_file_error_quark(), errno,
                                                strerror(errno));
            close(fd);
            expect->handler->error(error, expect->handler_ctx);
            return;
        }
    }

    struct timeval expect_timeout = {
        .tv_sec = expect->config->timeout > 0 ? expect->config->timeout : 10,
        .tv_usec = 0,
    };

    expect->fd = fd;
    event_set(&expect->event, fd, EV_READ|EV_TIMEOUT,
              expect_monitor_event_callback, expect);
    event_add(&expect->event, &expect_timeout);

    async_init(&expect->async_operation, &expect_monitor_async_operation);
    async_ref_set(expect->async_ref, &expect->async_operation);

    pool_ref(expect->pool);
}

static void
expect_monitor_timeout(void *ctx)
{
    struct expect_monitor_context *expect = ctx;
    expect->handler->timeout(expect->handler_ctx);
}

static void
expect_monitor_error(GError *error, void *ctx)
{
    struct expect_monitor_context *expect = ctx;
    expect->handler->error(error, expect->handler_ctx);
}

static const struct client_socket_handler expect_monitor_handler = {
    .success = expect_monitor_success,
    .timeout = expect_monitor_timeout,
    .error = expect_monitor_error,
};

/*
 * lb_monitor_class
 *
 */

static void
expect_monitor_run(struct pool *pool, const struct lb_monitor_config *config,
                   const struct sockaddr *address, size_t address_length,
                   const struct lb_monitor_handler *handler, void *handler_ctx,
                   struct async_operation_ref *async_ref)
{
    struct expect_monitor_context *expect = p_malloc(pool, sizeof(*expect));
    expect->pool = pool;
    expect->config = config;
    expect->handler = handler;
    expect->handler_ctx = handler_ctx;
    expect->async_ref = async_ref;

    const unsigned connect_timeout = config->timeout > 0
        ? config->timeout
        : 30;

    client_socket_new(pool, address->sa_family, SOCK_STREAM, 0,
                      address, address_length,
                      connect_timeout,
                      &expect_monitor_handler, expect,
                      async_ref);
}

const struct lb_monitor_class expect_monitor_class = {
    .run = expect_monitor_run,
};
