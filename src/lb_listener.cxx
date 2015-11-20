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
#include "util/Error.hxx"
#include "net/SocketDescriptor.hxx"
#include "net/SocketAddress.hxx"

#include <daemon/log.h>

void
lb_listener::OnAccept(SocketDescriptor &&new_fd, SocketAddress address)
{
    lb_connection_new(&instance, &config,
                      ssl_factory,
                      std::move(new_fd), address);
}

void
lb_listener::OnAcceptError(Error &&error)
{
    daemon_log(2, "%s\n", error.GetMessage());
}

/*
 * constructor
 *
 */

lb_listener::lb_listener(struct lb_instance &_instance,
                         const LbListenerConfig &_config)
    :instance(_instance), config(_config) {}

bool
lb_listener::Setup(Error &error)
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

    return Listen(config.bind_address.GetFamily(), SOCK_STREAM, 0,
                  config.bind_address,
                  error);
}

lb_listener::~lb_listener()
{
    if (ssl_factory != nullptr)
        ssl_factory_free(ssl_factory);
}

unsigned
lb_listener::FlushSSLSessionCache(long tm)
{
    return ssl_factory != nullptr
        ? ssl_factory_flush(*ssl_factory, tm)
        : 0;
}
