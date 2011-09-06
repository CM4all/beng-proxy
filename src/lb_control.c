/*
 * Listener on a control datagram socket.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "lb_control.h"
#include "lb_instance.h"
#include "lb_config.h"
#include "control-server.h"
#include "address-envelope.h"

#include <daemon/log.h>

static void
lb_control_packet(enum beng_control_command command,
                  const void *payload, size_t payload_length,
                  void *ctx)
{
    struct lb_control *control = ctx;

    (void)control;
    (void)payload;
    (void)payload_length;

    switch (command) {
    case CONTROL_NOP:
    case CONTROL_TCACHE_INVALIDATE:
        break;
    }
}

static void
lb_control_error(GError *error, G_GNUC_UNUSED void *ctx)
{
    daemon_log(2, "%s\n", error->message);
    g_error_free(error);
}

static const struct control_handler lb_control_handler = {
    .packet = lb_control_packet,
    .error = lb_control_error,
};

struct lb_control *
lb_control_new(struct lb_instance *instance,
               const struct lb_control_config *config,
               GError **error_r)
{
    struct pool *pool = pool_new_linear(instance->pool, "lb_control", 1024);

    struct lb_control *control = p_malloc(pool, sizeof(*control));
    control->pool = pool;
    control->instance = instance;

    control->server =
        control_server_new_envelope(pool, config->envelope,
                                    &lb_control_handler, control,
                                    error_r);
    if (control->server == NULL) {
        pool_unref(pool);
        return NULL;
    }

    return control;
}

void
lb_control_free(struct lb_control *control)
{
    control_server_free(control->server);

    pool_unref(control->pool);
}
