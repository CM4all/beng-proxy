/*
 * Listener on a control datagram socket.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_LB_CONTROL_H
#define BENG_PROXY_LB_CONTROL_H

#include "control_handler.hxx"
#include "io/Logger.hxx"

#include <memory>

struct LbInstance;
struct LbControlConfig;
class ControlServer;

class LbControl final : ControlHandler {
    const LLogger logger;

    LbInstance &instance;

    std::unique_ptr<ControlServer> server;

public:
    explicit LbControl(LbInstance &_instance);
    ~LbControl();

    void Open(const LbControlConfig &config);

    void Enable();
    void Disable();

private:
    void InvalidateTranslationCache(const void *payload,
                                    size_t payload_length);

    void EnableNode(const char *payload, size_t length);
    void FadeNode(const char *payload, size_t length);

    void QueryNodeStatus(ControlServer &control_server,
                         const char *payload, size_t length,
                         SocketAddress address);

    void QueryStats(ControlServer &control_server, SocketAddress address);

    /* virtual methods from class ControlHandler */
    void OnControlPacket(ControlServer &control_server,
                         enum beng_control_command command,
                         const void *payload, size_t payload_length,
                         SocketAddress address) override;

    void OnControlError(std::exception_ptr ep) override;
};

#endif
