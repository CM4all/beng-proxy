// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <max.kellermann@ionos.com>

#pragma once

#include "http/server/Handler.hxx"
#include "lib/avahi/ExplorerListener.hxx"

#include <map>
#include <memory>

struct LbContext;
struct LbPrometheusDiscoveryConfig;
namespace Avahi { class ServiceExplorer; }
class GrowingBuffer;
class AllocatedSocketAddress;

class LbPrometheusDiscovery final : public HttpServerRequestHandler, Avahi::ServiceExplorerListener {
	std::unique_ptr<Avahi::ServiceExplorer> explorer;

	std::map<std::string, InetAddress, std::less<>> members;

public:
	LbPrometheusDiscovery(const LbPrometheusDiscoveryConfig &config,
			      const LbContext &context) noexcept;

	~LbPrometheusDiscovery() noexcept;

	/* virtual methods from class HttpServerRequestHandler */
	void HandleHttpRequest(IncomingHttpRequest &request,
			       const StopwatchPtr &parent_stopwatch,
			       CancellablePointer &cancel_ptr) noexcept override;

private:
	[[gnu::pure]]
	GrowingBuffer GenerateJSON() const noexcept;

	/* virtual methods from class AvahiServiceExplorerListener */
	void OnAvahiNewObject(const std::string &key,
			      const InetAddress &address,
			      AvahiStringList *txt) noexcept override;
	void OnAvahiRemoveObject(const std::string &key) noexcept override;
};
