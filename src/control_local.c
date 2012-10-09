/*
 * Control server on an implicitly configured local socket.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "control_local.h"
#include "control_server.h"
#include "pool.h"

#include <assert.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <stdio.h>
#include <unistd.h>

struct control_local {
    struct pool *pool;

    const char *prefix;

    const struct control_handler *handler;
    void *handler_ctx;

    struct pool *server_pool;
    struct control_server *server;
};

/*
 * control_handler
 *
 */

static bool
control_local_raw(const void *data, size_t length,
                  const struct sockaddr *address, size_t address_length,
                  int uid, void *ctx)
{
    struct control_local *cl = ctx;

    if (uid < 0 || (uid != 0 && (uid_t)uid != geteuid()))
        /* only root and the beng-proxy user are allowed to send
           commands to the implicit control channel */
        return false;

    return cl->handler->raw == NULL ||
        cl->handler->raw(data, length, address, address_length,
                         uid, cl->handler_ctx);
}

static void
control_local_packet(enum beng_control_command command,
                     const void *payload, size_t payload_length,
                     const struct sockaddr *address, size_t address_length,
                     void *ctx)
{
    struct control_local *cl = ctx;

    cl->handler->packet(command, payload, payload_length,
                        address, address_length,
                        cl->handler_ctx);
}

static void
control_local_error(GError *error, void *ctx)
{
    struct control_local *cl = ctx;

    cl->handler->error(error, cl->handler_ctx);
}

static const struct control_handler control_local_handler = {
    .raw = control_local_raw,
    .packet = control_local_packet,
    .error = control_local_error,
};

/*
 * public
 *
 */

struct control_local *
control_local_new(struct pool *pool, const char *prefix,
                  const struct control_handler *handler, void *ctx)
{
    struct control_local *cl = p_malloc(pool, sizeof(*cl));
    cl->pool = pool;
    cl->prefix = prefix;
    cl->handler = handler;
    cl->handler_ctx = ctx;
    cl->server = NULL;

    return cl;
}

static void
control_local_close(struct control_local *cl)
{
    if (cl->server != NULL) {
        control_server_free(cl->server);
        cl->server = NULL;
    }

    if (cl->server_pool != NULL) {
        pool_unref(cl->server_pool);
        pool_trash(cl->server_pool);
        cl->server_pool = NULL;
    }
}

void
control_local_free(struct control_local *cl)
{
    control_local_close(cl);
}

bool
control_local_open(struct control_local *cl, GError **error_r)
{
    control_local_close(cl);

    struct sockaddr_un sa;
    sa.sun_family = AF_UNIX;
    sa.sun_path[0] = '\0';
    sprintf(sa.sun_path + 1, "%s%d", cl->prefix, (int)getpid());

    cl->server_pool = pool_new_libc(cl->pool, "control_local");
    cl->server = control_server_new(cl->server_pool,
                                    (const struct sockaddr *)&sa,
                                    SUN_LEN(&sa) + 1 + strlen(sa.sun_path + 1),
                                    &control_local_handler, cl,
                                    error_r);
    if (cl->server == NULL) {
        control_local_close(cl);
        return false;
    }

    return true;
}

struct control_server *
control_local_get(struct control_local *cl)
{
    assert(cl != NULL);
    assert(cl->server != NULL);

    return cl->server;
}
