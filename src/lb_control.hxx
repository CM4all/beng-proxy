/*
 * Listener on a control datagram socket.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_LB_CONTROL_H
#define BENG_PROXY_LB_CONTROL_H

#include "control_handler.hxx"

#include <inline/list.h>

#include <memory>

struct LbInstance;
struct LbControlConfig;
struct ControlServer;
class Error;

struct LbControl final : ControlHandler {
    struct list_head siblings;

    LbInstance &instance;

    std::unique_ptr<ControlServer> server;

    explicit LbControl(LbInstance &_instance);
    ~LbControl();

    void Open(const LbControlConfig &config);

    void Enable();
    void Disable();

    /* virtual methods from class ControlHandler */
    void OnControlPacket(ControlServer &control_server,
                         enum beng_control_command command,
                         const void *payload, size_t payload_length,
                         SocketAddress address) override;

    void OnControlError(Error &&error) override;
};

#endif
