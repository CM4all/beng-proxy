/*
 * Server side part of the "control" protocol.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "control_server.hxx"
#include "pool.hxx"
#include "net/SocketAddress.hxx"
#include "util/ByteOrder.hxx"

#include <assert.h>
#include <string.h>

void
control_server_decode(const void *data, size_t length,
                      SocketAddress address,
                      const struct control_handler *handler, void *handler_ctx)
{
    assert(handler != nullptr);
    assert(handler->packet != nullptr);
    assert(handler->error != nullptr);

    /* verify the magic number */

    const uint32_t *magic = (const uint32_t *)data;

    if (length < sizeof(*magic) || FromBE32(*magic) != control_magic) {
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
        const struct beng_control_header *header =
            (const struct beng_control_header *)data;
        if (length < sizeof(*header)) {
            GError *error = g_error_new(control_server_quark(), 0,
                                        "partial header (length=%zu)", length);
            handler->error(error, handler_ctx);
            return;
        }

        size_t payload_length = FromBE16(header->length);
        beng_control_command command = (beng_control_command)
            FromBE16(header->command);

        data = header + 1;
        length -= sizeof(*header);

        const char *payload = (const char *)data;
        if (length < payload_length) {
            GError *error = g_error_new(control_server_quark(), 0,
                                        "partial payload (length=%zu, expected=%zu)",
                                        length, payload_length);
            handler->error(error, handler_ctx);
            return;
        }

        /* this command is ok, pass it to the callback */

        handler->packet(command, payload_length > 0 ? payload : nullptr,
                        payload_length,
                        address,
                        handler_ctx);

        payload_length = ((payload_length + 3) | 3) - 3; /* apply padding */

        data = payload + payload_length;
        length -= payload_length;
    }
}

void
ControlServer::OnUdpDatagram(const void *data, size_t length,
                             SocketAddress address, int uid)
{
    if (handler->raw != nullptr &&
        !handler->raw(data, length, address, uid, handler_ctx))
        /* discard datagram if raw() returns false */
        return;

    control_server_decode(data, length, address,
                          handler, handler_ctx);
}

void
ControlServer::OnUdpError(GError *error)
{
    handler->error(error, handler_ctx);
}

ControlServer *
control_server_new_port(const char *host_and_port, int default_port,
                        const struct in_addr *group,
                        const struct control_handler *handler, void *ctx,
                        GError **error_r)
{
    assert(host_and_port != nullptr);
    assert(handler != nullptr);
    assert(handler->packet != nullptr);
    assert(handler->error != nullptr);

    auto cs = new ControlServer();
    cs->udp = udp_listener_port_new(host_and_port, default_port,
                                    *cs, error_r);
    if (cs->udp == nullptr)
        return nullptr;

    if (group != nullptr && !udp_listener_join4(cs->udp, group, error_r)) {
        udp_listener_free(cs->udp);
        return nullptr;
    }

    cs->handler = handler;
    cs->handler_ctx = ctx;

    return cs;
}

ControlServer *
control_server_new(SocketAddress address,
                   const struct control_handler *handler, void *ctx,
                   GError **error_r)
{
    assert(handler != nullptr);
    assert(handler->packet != nullptr);
    assert(handler->error != nullptr);

    auto cs = new ControlServer();
    cs->udp = udp_listener_new(address, *cs, error_r);
    if (cs->udp == nullptr)
        return nullptr;

    cs->handler = handler;
    cs->handler_ctx = ctx;

    return cs;
}

void
control_server_free(ControlServer *cs)
{
    udp_listener_free(cs->udp);
    delete cs;
}

void
control_server_enable(ControlServer *cs)
{
    udp_listener_enable(cs->udp);
}

void
control_server_disable(ControlServer *cs)
{
    udp_listener_disable(cs->udp);
}

void
control_server_set_fd(ControlServer *cs, int fd)
{
    udp_listener_set_fd(cs->udp, fd);
}

bool
control_server_reply(ControlServer *cs, struct pool *pool,
                     SocketAddress address,
                     enum beng_control_command command,
                     const void *payload, size_t payload_length,
                     GError **error_r)
{
    assert(cs != nullptr);
    assert(cs->udp != nullptr);

    struct beng_control_header *header = (struct beng_control_header *)
        p_malloc(pool, sizeof(*header) + payload_length);
    header->length = ToBE16(payload_length);
    header->command = ToBE16(command);
    memcpy(header + 1, payload, payload_length);

    return udp_listener_reply(cs->udp, address,
                              header, sizeof(*header) + payload_length,
                              error_r);
}
