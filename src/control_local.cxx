/*
 * Control server on an implicitly configured local socket.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "control_local.hxx"
#include "control_server.hxx"
#include "net/SocketAddress.hxx"

#include <utility>

#include <assert.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <stdio.h>
#include <unistd.h>

struct LocalControl final : ControlHandler {
    const char *prefix;

    ControlHandler &handler;

    ControlServer *server;

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

    void OnControlError(Error &&error) override;
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
LocalControl::OnControlError(Error &&error)
{
    handler.OnControlError(std::move(error));
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

static void
control_local_close(LocalControl *cl)
{
    delete cl->server;
    cl->server = nullptr;
}

void
control_local_free(LocalControl *cl)
{
    control_local_close(cl);
    delete cl;
}

bool
control_local_open(LocalControl *cl, Error &error_r)
{
    control_local_close(cl);

    struct sockaddr_un sa;
    sa.sun_family = AF_UNIX;
    sa.sun_path[0] = '\0';
    sprintf(sa.sun_path + 1, "%s%d", cl->prefix, (int)getpid());

    cl->server = new ControlServer(*cl);
    if (!cl->server->Open(SocketAddress((const struct sockaddr *)&sa,
                                        SUN_LEN(&sa) + 1 + strlen(sa.sun_path + 1)),
                          error_r)) {
        control_local_close(cl);
        return false;
    }

    return true;
}

ControlServer *
control_local_get(LocalControl *cl)
{
    assert(cl != nullptr);
    assert(cl->server != nullptr);

    return cl->server;
}
