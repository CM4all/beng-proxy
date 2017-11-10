/*
 * Copyright 2007-2017 Content Management AG
 * All rights reserved.
 *
 * author: Max Kellermann <mk@cm4all.com>
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * - Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 *
 * - Redistributions in binary form must reproduce the above copyright
 * notice, this list of conditions and the following disclaimer in the
 * documentation and/or other materials provided with the
 * distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE
 * FOUNDATION OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED
 * OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "control_local.hxx"
#include "control_server.hxx"
#include "net/SocketConfig.hxx"

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

    void OnControlError(std::exception_ptr ep) noexcept override;
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
LocalControl::OnControlError(std::exception_ptr ep) noexcept
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

    SocketConfig config;
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
