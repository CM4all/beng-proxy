// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#include "GotoConfig.hxx"
#include "ClusterConfig.hxx"
#include "PrometheusExporterConfig.hxx"

#ifdef HAVE_AVAHI
#include "PrometheusDiscoveryConfig.hxx"

bool
LbBranchConfig::HasZeroConf() const noexcept
{
	if (fallback.HasZeroConf())
		return true;

	for (const auto &i : conditions)
		if (i.HasZeroConf())
			return true;

	return false;
}

#endif

LbProtocol
LbGotoConfig::GetProtocol() const noexcept
{
	assert(IsDefined());

	struct GetProtocolHelper {
		LbProtocol operator()(std::monostate) const noexcept {
			assert(false);
			gcc_unreachable();
		}

		LbProtocol operator()(const LbClusterConfig *cluster) const noexcept {
			return cluster->protocol;
		}

		LbProtocol operator()(const LbBranchConfig *branch) const noexcept {
			return branch->GetProtocol();
		}

		LbProtocol operator()(const LbLuaHandlerConfig *) const noexcept {
			return LbProtocol::HTTP;
		}

		LbProtocol operator()(const LbTranslationHandlerConfig *) const noexcept {
			return LbProtocol::HTTP;
		}

		LbProtocol operator()(const LbPrometheusExporterConfig *) const noexcept {
			return LbProtocol::HTTP;
		}

#ifdef HAVE_AVAHI
		LbProtocol operator()(const LbPrometheusDiscoveryConfig *) const noexcept {
			return LbProtocol::HTTP;
		}
#endif // HAVE_AVAHI

		LbProtocol operator()(const LbSimpleHttpResponse &) const noexcept {
			return LbProtocol::HTTP;
		}
	};

	return std::visit(GetProtocolHelper{}, destination);
}

const char *
LbGotoConfig::GetName() const noexcept
{
	assert(IsDefined());

	struct GetNameHelper {
		const char *operator()(std::monostate) const noexcept {
			assert(false);
			gcc_unreachable();
		}

		const char *operator()(const LbClusterConfig *cluster) const noexcept {
			return cluster->name.c_str();
		}

		const char *operator()(const LbBranchConfig *branch) const noexcept {
			return branch->name.c_str();
		}

		const char *operator()(const LbLuaHandlerConfig *lua) const noexcept {
			return lua->name.c_str();
		}

		const char *operator()(const LbTranslationHandlerConfig *translation) const noexcept {
			return translation->name.c_str();
		}

		const char *operator()(const LbPrometheusExporterConfig *exporter) const noexcept {
			return exporter->name.c_str();
		}

#ifdef HAVE_AVAHI
		const char *operator()(const LbPrometheusDiscoveryConfig *discovery) const noexcept {
			return discovery->name.c_str();
		}
#endif // HAVE_AVAHI

		const char *operator()(const LbSimpleHttpResponse &) const noexcept {
			return "response";
		}
	};

	return std::visit(GetNameHelper{}, destination);
}

#ifdef HAVE_AVAHI

bool
LbGotoConfig::HasZeroConf() const noexcept
{
	struct HasZeroConfHelper {
		bool operator()(std::monostate) const noexcept {
			return false;
		}

		bool operator()(const LbClusterConfig *cluster) const noexcept {
			return cluster->HasZeroConf();
		}

		bool operator()(const LbBranchConfig *branch) const noexcept {
			return branch->HasZeroConf();
		}

		bool operator()(const LbLuaHandlerConfig *) const noexcept {
			return false;
		}

		bool operator()(const LbTranslationHandlerConfig *) const noexcept {
			return false;
		}

		bool operator()(const LbPrometheusExporterConfig *) const noexcept {
			return false;
		}

#ifdef HAVE_AVAHI
		bool operator()(const LbPrometheusDiscoveryConfig *) const noexcept {
			return true;
		}
#endif // HAVE_AVAHI

		bool operator()(const LbSimpleHttpResponse &) const noexcept {
			return false;
		}
	};

	return std::visit(HasZeroConfHelper{}, destination);
}

#endif
