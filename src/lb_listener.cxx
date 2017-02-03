/*
 * Listener on a TCP port.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "lb_listener.hxx"
#include "lb_instance.hxx"
#include "lb_connection.hxx"
#include "lb_config.hxx"
#include "ssl/ssl_factory.hxx"
#include "ssl/DbSniCallback.hxx"
#include "net/SocketDescriptor.hxx"
#include "net/SocketAddress.hxx"
#include "util/Exception.hxx"

#include <daemon/log.h>

void
LbListener::OnAccept(SocketDescriptor &&new_fd, SocketAddress address)
{
    lb_connection_new(instance, config,
                      ssl_factory,
                      std::move(new_fd), address);
}

void
LbListener::OnAcceptError(std::exception_ptr ep)
{
    daemon_log(2, "%s\n", GetFullMessage(ep).c_str());
}

/*
 * constructor
 *
 */

LbListener::LbListener(LbInstance &_instance,
                       const LbListenerConfig &_config)
    :ServerSocket(_instance.event_loop),
     instance(_instance), config(_config)
{
}

void
LbListener::Setup()
{
    assert(ssl_factory == nullptr);

    if (config.ssl) {
        /* prepare SSL support */

        std::unique_ptr<SslSniCallback> sni_callback;
        if (config.cert_db != nullptr) {
            auto &cert_cache = instance.GetCertCache(*config.cert_db);
            sni_callback.reset(new DbSslSniCallback(cert_cache));
        }

        ssl_factory = ssl_factory_new_server(config.ssl_config,
                                             std::move(sni_callback));
    }

    Listen(config.bind_address.GetFamily(), SOCK_STREAM, 0,
           config.bind_address,
           config.reuse_port,
           config.interface.empty() ? nullptr : config.interface.c_str());

    if (config.destination.GetProtocol() == LbProtocol::HTTP ||
        config.ssl)
        SetTcpDeferAccept(10);
}

LbListener::~LbListener()
{
    if (ssl_factory != nullptr)
        ssl_factory_free(ssl_factory);
}

unsigned
LbListener::FlushSSLSessionCache(long tm)
{
    return ssl_factory != nullptr
        ? ssl_factory_flush(*ssl_factory, tm)
        : 0;
}
