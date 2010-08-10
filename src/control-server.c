/*
 * Server side part of the "control" protocol.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "control-server.h"
#include "udp-listener.h"

#include <daemon/log.h>

#include <glib.h>
#include <assert.h>

struct control_server {
    struct udp_listener *udp;

    const struct control_handler *handler;
    void *handler_ctx;
};

void
control_server_decode(const void *data, size_t length,
                      const struct control_handler *handler, void *handler_ctx)
{
    /* verify the magic number */

    const uint32_t *magic = data;

    if (length < sizeof(*magic) ||
        GUINT32_FROM_BE(*magic) != beng_control_magic) {
        daemon_log(2, "wrong magic\n");
        return;
    }

    data = magic + 1;
    length -= sizeof(*magic);

    if (length % 4 != 0) {
        daemon_log(2, "odd control packet (length=%zu)\n", length);
        return;
    }

    /* now decode all commands */

    while (length > 0) {
        const struct beng_control_header *header = data;
        if (length < sizeof(*header)) {
            daemon_log(2, "partial header (length=%zu)\n", length);
            return;
        }

        size_t payload_length = GUINT16_FROM_BE(header->length);
        enum beng_control_command command = GUINT16_FROM_BE(header->command);

        data = header + 1;
        length -= sizeof(*header);

        const char *payload = data;
        if (length < payload_length) {
            daemon_log(2, "partial payload (length=%zu, expected=%zu)\n",
                       length, payload_length);
            return;
        }

        /* this command is ok, pass it to the callback */

        handler->packet(command, payload_length > 0 ? payload : NULL,
                        payload_length, handler_ctx);

        payload_length = ((payload_length + 3) | 3) - 3; /* apply padding */

        data = payload + payload_length;
        length -= payload_length;
    }
}

static void
control_server_udp_callback(const void *data, size_t length,
                            G_GNUC_UNUSED const struct sockaddr *addr,
                            G_GNUC_UNUSED size_t addrlen,
                            void *ctx)
{
    struct control_server *cs = ctx;

    control_server_decode(data, length, cs->handler, cs->handler_ctx);
}

struct control_server *
control_server_new(pool_t pool, const char *host_and_port, int default_port,
                   const struct in_addr *group,
                   const struct control_handler *handler, void *ctx)
{
    assert(pool != NULL);
    assert(host_and_port != NULL);
    assert(handler != NULL);
    assert(handler->packet != NULL);

    struct control_server *cs = p_malloc(pool, sizeof(*cs));
    cs->udp = udp_listener_port_new(pool, host_and_port, default_port,
                                    control_server_udp_callback, cs);
    if (cs->udp == NULL)
        return NULL;

    if (group != NULL && !udp_listener_join4(cs->udp, group)) {
        udp_listener_free(cs->udp);
        return NULL;
    }

    cs->handler = handler;
    cs->handler_ctx = ctx;

    return cs;
}

void
control_server_free(struct control_server *cs)
{
    udp_listener_free(cs->udp);
}
