/*
 * Control server on an implicitly configured local socket.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "control_local.hxx"
#include "control_server.hxx"
#include "net/UdpListenerConfig.hxx"

#include <memory>
#include <utility>

#include <assert.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <stdio.h>
#include <unistd.h>

struct LocalControl final : ControlHandler {
    const char *prefix;

    ControlHandler &handler;

    std::unique_ptr<ControlServer> server;

    explicit LocalControl(ControlHandler &_handler)
        :handler(_handler) {}

    /* virtual methods from class ControlHandler */
    bool OnControlRaw(const void *data, size_t length,
                      SocketAddress address,
                      int uid) override;

    void OnControlPacket(ControlServer &control_server,
                         enum beng_control_command command,
                         const void *payload, size_t payload_length,
                         SocketAddress address) override;

    void OnControlError(std::exception_ptr ep) override;
};

/*
 * control_handler
 *
 */

bool
LocalControl::OnControlRaw(const void *data, size_t length,
                           SocketAddress address,
                           int uid)
{
    if (uid < 0 || (uid != 0 && (uid_t)uid != geteuid()))
        /* only root and the beng-proxy user are allowed to send
           commands to the implicit control channel */
        return false;

    return handler.OnControlRaw(data, length, address, uid);
}

void
LocalControl::OnControlPacket(ControlServer &control_server,
                              enum beng_control_command command,
                              const void *payload, size_t payload_length,
                              SocketAddress address)
{
    handler.OnControlPacket(control_server, command,
                            payload, payload_length, address);
}

void
LocalControl::OnControlError(std::exception_ptr ep)
{
    handler.OnControlError(ep);
}

/*
 * public
 *
 */

LocalControl *
control_local_new(const char *prefix, ControlHandler &handler)
{
    auto cl = new LocalControl(handler);
    cl->prefix = prefix;
    cl->server = nullptr;

    return cl;
}

void
control_local_free(LocalControl *cl)
{
    delete cl;
}

void
control_local_open(LocalControl *cl, EventLoop &event_loop)
{
    cl->server.reset();

    struct sockaddr_un sa;
    sa.sun_family = AF_UNIX;
    sa.sun_path[0] = '\0';
    sprintf(sa.sun_path + 1, "%s%d", cl->prefix, (int)getpid());

    UdpListenerConfig config;
    config.bind_address = SocketAddress((const struct sockaddr *)&sa,
                                        SUN_LEN(&sa) + 1 + strlen(sa.sun_path + 1)),
    config.pass_cred = true;

    cl->server = std::make_unique<ControlServer>(event_loop, *cl, config);
}

ControlServer *
control_local_get(LocalControl *cl)
{
    assert(cl != nullptr);
    assert(cl->server != nullptr);

    return cl->server.get();
}
