// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#include "Listener.hxx"
#include "Instance.hxx"
#include "ListenerConfig.hxx"
#include "HttpConnection.hxx"
#include "TcpConnection.hxx"
#include "pool/UniquePtr.hxx"
#include "ssl/Factory.hxx"
#include "ssl/DbCertCallback.hxx"
#include "ssl/AlpnProtos.hxx"
#include "fs/FilteredSocket.hxx"
#include "net/ClientAccounting.hxx"
#include "net/SocketAddress.hxx"
#include "util/SpanCast.hxx"
#include "lb_features.h"

#ifdef HAVE_AVAHI
#include "lib/avahi/Service.hxx"
#include "lib/avahi/Publisher.hxx"

inline std::unique_ptr<Avahi::Service>
LbListener::MakeAvahiService() const noexcept
{
	if (!config.HasZeroconfPublisher())
		return {};

	/* ask the kernel for the effective address via getsockname(),
	   because it may have changed, e.g. if the kernel has
	   selected a port for us */
	if (const auto local_address = GetLocalAddress();
	    local_address.IsDefined())
		return std::make_unique<Avahi::Service>(config.zeroconf_service.c_str(),
							config.GetZeroconfInterface(), local_address,
							config.v6only);

	return {};
}

#endif // HAVE_AVAHI

UniqueSocketDescriptor
LbListener::OnFilteredSocketAccept(UniqueSocketDescriptor s,
				   SocketAddress address)
{
	if (client_accounting)
		if (auto *per_client = client_accounting->Get(address);
		    per_client != nullptr && !per_client->Check())
			s.Close();

	return s;
}

static std::unique_ptr<SslFactory>
MakeSslFactory(const LbListenerConfig &config,
	       LbInstance &instance)
{
	if (!config.ssl)
		return nullptr;

	/* prepare SSL support */

	std::unique_ptr<SslCertCallback> sni_callback;
#ifdef ENABLE_CERTDB
	if (config.cert_db != nullptr) {
		auto &cert_cache = instance.GetCertCache(*config.cert_db);
		sni_callback.reset(new DbSslCertCallback(cert_cache));
	}
#else
	(void)instance;
#endif

	auto ssl_factory = std::make_unique<SslFactory>(config.ssl_config,
							std::move(sni_callback));

#ifdef ENABLE_CERTDB
	if (config.cert_db != nullptr)
		ssl_factory->AddAlpn(alpn_acme_tls1);
#endif

	/* we use the listener name as OpenSSL session_id_context,
	   because listener names are unique, so I hope this should be
	   good enough */
	ssl_factory->SetSessionIdContext(AsBytes(config.name));

#ifdef HAVE_NGHTTP2
	if (config.GetAlpnHttp2())
		ssl_factory->AddAlpn(alpn_http_any);
#endif

	return ssl_factory;
}

void
LbListener::OnFilteredSocketConnect(PoolPtr pool,
				    UniquePoolPtr<FilteredSocket> socket,
				    SocketAddress address,
				    const SslFilter *ssl_filter) noexcept
try {
	LbHttpConnection *http_connection;

	switch (protocol) {
	case LbProtocol::HTTP:
		http_connection = NewLbHttpConnection(instance, *this,
						      destination,
						      std::move(pool),
						      std::move(socket),
						      ssl_filter,
						      address);

		if (client_accounting)
			if (auto *per_client = client_accounting->Get(address);
			    per_client != nullptr)
				per_client->AddConnection(*http_connection);

		break;

	case LbProtocol::TCP:
		assert(std::holds_alternative<LbCluster *>(destination.destination));

		LbTcpConnection::New(instance, config,
				     *std::get<LbCluster *>(destination.destination),
				     std::move(pool),
				     std::move(socket),
				     address);
		break;
	}
} catch (...) {
	logger(1, "Failed to setup accepted connection: ",
	       std::current_exception());
}

void
LbListener::OnFilteredSocketError(std::exception_ptr ep) noexcept
{
	logger(2, "Failed to accept: ", ep);
}

/*
 * constructor
 *
 */

LbListener::LbListener(LbInstance &_instance,
		       const LbListenerConfig &_config)
	:instance(_instance), config(_config),
	 listener(instance.root_pool, instance.event_loop,
		  MakeSslFactory(config, instance),
		  *this, config.Create(SOCK_STREAM)),
#ifdef HAVE_AVAHI
	 avahi_service(MakeAvahiService()),
#endif
	 logger("listener " + config.name),
	 protocol(config.destination.GetProtocol())
{
	if (config.max_connections_per_ip > 0)
		client_accounting = std::make_unique<ClientAccountingMap>(GetEventLoop(),
									  config.max_connections_per_ip);

#ifdef HAVE_AVAHI
	if (avahi_service)
		instance.GetAvahiPublisher().AddService(*avahi_service);
#endif
}

LbListener::~LbListener() noexcept
{
#ifdef HAVE_AVAHI
	if (avahi_service)
		instance.GetAvahiPublisher().RemoveService(*avahi_service);
#endif
}

#ifdef HAVE_AVAHI

void
LbListener::SetZeroconfVisible(bool _visible) noexcept
{
	assert(avahi_service);

	if (avahi_service->visible == _visible)
		return;

	avahi_service->visible = _visible;
	instance.GetAvahiPublisher().UpdateServices();
}

#endif

void
LbListener::Scan(LbGotoMap &goto_map)
{
	destination = goto_map.GetInstance(config.destination);
}
