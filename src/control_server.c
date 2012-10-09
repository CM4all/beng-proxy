/*
 * Server side part of the "control" protocol.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "control_server.h"
#include "udp-listener.h"
#include "address-envelope.h"

#include <glib.h>
#include <assert.h>
#include <string.h>

struct control_server {
    struct udp_listener *udp;

    const struct control_handler *handler;
    void *handler_ctx;
};

void
control_server_decode(const void *data, size_t length,
                      const struct sockaddr *address, size_t address_length,
                      const struct control_handler *handler, void *handler_ctx)
{
    assert(handler != NULL);
    assert(handler->packet != NULL);
    assert(handler->error != NULL);

    /* verify the magic number */

    const uint32_t *magic = data;

    if (length < sizeof(*magic) || GUINT32_FROM_BE(*magic) != control_magic) {
        GError *error = g_error_new_literal(control_server_quark(), 0,
                                            "wrong magic");
        handler->error(error, handler_ctx);
        return;
    }

    data = magic + 1;
    length -= sizeof(*magic);

    if (length % 4 != 0) {
        GError *error = g_error_new(control_server_quark(), 0,
                                    "odd control packet (length=%zu)", length);
        handler->error(error, handler_ctx);
        return;
    }

    /* now decode all commands */

    while (length > 0) {
        const struct beng_control_header *header = data;
        if (length < sizeof(*header)) {
            GError *error = g_error_new(control_server_quark(), 0,
                                        "partial header (length=%zu)", length);
            handler->error(error, handler_ctx);
            return;
        }

        size_t payload_length = GUINT16_FROM_BE(header->length);
        enum beng_control_command command = GUINT16_FROM_BE(header->command);

        data = header + 1;
        length -= sizeof(*header);

        const char *payload = data;
        if (length < payload_length) {
            GError *error = g_error_new(control_server_quark(), 0,
                                        "partial payload (length=%zu, expected=%zu)",
                                        length, payload_length);
            handler->error(error, handler_ctx);
            return;
        }

        /* this command is ok, pass it to the callback */

        handler->packet(command, payload_length > 0 ? payload : NULL,
                        payload_length,
                        address, address_length,
                        handler_ctx);

        payload_length = ((payload_length + 3) | 3) - 3; /* apply padding */

        data = payload + payload_length;
        length -= payload_length;
    }
}

static void
control_server_udp_datagram(const void *data, size_t length,
                            const struct sockaddr *address,
                            size_t address_length,
                            void *ctx)
{
    struct control_server *cs = ctx;

    if (cs->handler->raw != NULL)
        cs->handler->raw(data, length, address, address_length,
                         cs->handler_ctx);

    control_server_decode(data, length, address, address_length,
                          cs->handler, cs->handler_ctx);
}

static void
control_server_udp_error(GError *error, void *ctx)
{
    struct control_server *cs = ctx;

    cs->handler->error(error, cs->handler_ctx);
}

static const struct udp_handler control_server_udp_handler = {
    .datagram = control_server_udp_datagram,
    .error = control_server_udp_error,
};

struct control_server *
control_server_new(struct pool *pool,
                   const char *host_and_port, int default_port,
                   const struct in_addr *group,
                   const struct control_handler *handler, void *ctx,
                   GError **error_r)
{
    assert(pool != NULL);
    assert(host_and_port != NULL);
    assert(handler != NULL);
    assert(handler->packet != NULL);
    assert(handler->error != NULL);

    struct control_server *cs = p_malloc(pool, sizeof(*cs));
    cs->udp = udp_listener_port_new(pool, host_and_port, default_port,
                                    &control_server_udp_handler, cs,
                                    error_r);
    if (cs->udp == NULL)
        return NULL;

    if (group != NULL && !udp_listener_join4(cs->udp, group, error_r)) {
        udp_listener_free(cs->udp);
        return NULL;
    }

    cs->handler = handler;
    cs->handler_ctx = ctx;

    return cs;
}

struct control_server *
control_server_new_envelope(struct pool *pool,
                            const struct address_envelope *envelope,
                            const struct control_handler *handler, void *ctx,
                            GError **error_r)
{
    assert(pool != NULL);
    assert(envelope != NULL);
    assert(handler != NULL);
    assert(handler->packet != NULL);
    assert(handler->error != NULL);

    struct control_server *cs = p_malloc(pool, sizeof(*cs));
    cs->udp = udp_listener_new(pool, &envelope->address, envelope->length,
                               &control_server_udp_handler, cs,
                               error_r);
    if (cs->udp == NULL)
        return NULL;

    cs->handler = handler;
    cs->handler_ctx = ctx;

    return cs;
}

void
control_server_free(struct control_server *cs)
{
    udp_listener_free(cs->udp);
}

void
control_server_set_fd(struct control_server *cs, int fd)
{
    udp_listener_set_fd(cs->udp, fd);
}

bool
control_server_reply(struct control_server *cs, struct pool *pool,
                     const struct sockaddr *address, size_t address_length,
                     enum beng_control_command command,
                     const void *payload, size_t payload_length,
                     GError **error_r)
{
    assert(cs != NULL);
    assert(cs->udp != NULL);
    assert(address != NULL);
    assert(address_length > 0);

    struct beng_control_header *header =
        p_malloc(pool, sizeof(*header) + payload_length);
    header->length = GUINT16_TO_BE(payload_length);
    header->command = GUINT16_TO_BE(command);
    memcpy(header + 1, payload, payload_length);

    return udp_listener_reply(cs->udp, address, address_length,
                              header, sizeof(*header) + payload_length,
                              error_r);
}
