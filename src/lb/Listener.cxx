/*
 * Listener on a TCP port.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "Listener.hxx"
#include "lb_instance.hxx"
#include "ListenerConfig.hxx"
#include "HttpConnection.hxx"
#include "TcpConnection.hxx"
#include "ssl/ssl_factory.hxx"
#include "ssl/DbSniCallback.hxx"
#include "net/SocketAddress.hxx"

void
LbListener::OnAccept(UniqueSocketDescriptor &&new_fd, SocketAddress address)
try {
    switch (config.destination.GetProtocol()) {
    case LbProtocol::HTTP:
        NewLbHttpConnection(instance, config, destination,
                            ssl_factory,
                            std::move(new_fd), address);
        break;

    case LbProtocol::TCP:
        assert(destination.cluster != nullptr);

        LbTcpConnection::New(instance, config, *destination.cluster,
                             ssl_factory,
                             std::move(new_fd), address);
        break;
    }
} catch (...) {
    logger(1, "Failed to setup accepted connection: ",
           std::current_exception());
 }

void
LbListener::OnAcceptError(std::exception_ptr ep)
{
    logger(2, "Failed to accept: ", ep);
}

/*
 * constructor
 *
 */

LbListener::LbListener(LbInstance &_instance,
                       const LbListenerConfig &_config)
    :ServerSocket(_instance.event_loop),
     instance(_instance), config(_config),
     logger("listener " + config.name)
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

    Listen(config.bind_address,
           config.reuse_port,
           config.interface.empty() ? nullptr : config.interface.c_str());

    if (config.destination.GetProtocol() == LbProtocol::HTTP ||
        config.ssl)
        SetTcpDeferAccept(10);
}

void
LbListener::Scan(LbGotoMap &goto_map)
{
    destination = goto_map.GetInstance(config.destination);
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
