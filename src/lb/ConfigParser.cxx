// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#include "Config.hxx"
#include "PrometheusExporterConfig.hxx"
#include "Check.hxx"
#include "access_log/ConfigParser.hxx"
#include "lib/fmt/RuntimeError.hxx"
#include "io/config/FileLineParser.hxx"
#include "io/config/ConfigParser.hxx"
#include "net/Parser.hxx"
#include "net/AddressInfo.hxx"
#include "net/control/Protocol.hxx"
#include "http/Status.hxx"
#include "uri/Verify.hxx"
#include "util/StringAPI.hxx"
#include "util/StringCompare.hxx"
#include "util/CharUtil.hxx"

#ifdef HAVE_AVAHI
#include "lib/avahi/Check.hxx"
#endif

#include <utility> // for std::unreachable()

#include <assert.h>
#include <stdlib.h>
#include <string.h>

class LbConfigParser final : public NestedConfigParser {
	LbConfig &config;

	AccessLogConfig *current_access_log;

	class Control final : public ConfigParser {
		LbConfigParser &parent;
		LbControlConfig config;

	public:
		explicit Control(LbConfigParser &_parent)
			:parent(_parent) {}

	protected:
		/* virtual methods from class ConfigParser */
		void ParseLine(FileLineParser &line) override;
		void Finish() override;
	};

	class CertDatabase final : public ConfigParser {
		LbConfigParser &parent;
		LbCertDatabaseConfig config;

	public:
		CertDatabase(LbConfigParser &_parent, const char *_name)
			:parent(_parent), config(_name) {}

	protected:
		/* virtual methods from class ConfigParser */
		void ParseLine(FileLineParser &line) override;
		void Finish() override;
	};

	class Monitor final : public ConfigParser {
		LbConfigParser &parent;
		LbMonitorConfig config;

	public:
		Monitor(LbConfigParser &_parent, const char *_name)
			:parent(_parent), config(_name) {}

	protected:
		/* virtual methods from class ConfigParser */
		void ParseLine(FileLineParser &line) override;
		void Finish() override;
	};

	class Node final : public ConfigParser {
		LbConfigParser &parent;
		LbNodeConfig config;

	public:
		Node(LbConfigParser &_parent, const char *_name)
			:parent(_parent), config(_name) {}

	protected:
		/* virtual methods from class ConfigParser */
		void ParseLine(FileLineParser &line) override;
		void Finish() override;
	};

	class Cluster final : public ConfigParser {
		LbConfigParser &parent;
		LbClusterConfig config;

	public:
		Cluster(LbConfigParser &_parent, const char *_name)
			:parent(_parent), config(_name) {}

	protected:
		/* virtual methods from class ConfigParser */
		void ParseLine(FileLineParser &line) override;
		void Finish() override;
	};

	class Branch final : public ConfigParser {
		LbConfigParser &parent;
		LbBranchConfig config;

	public:
		Branch(LbConfigParser &_parent, const char *_name)
			:parent(_parent), config(_name) {}

	private:
		void AddGoto(LbGotoConfig &&destination, FileLineParser &line);

	protected:
		/* virtual methods from class ConfigParser */
		void ParseLine(FileLineParser &line) override;
		void Finish() override;
	};

#ifdef HAVE_LUA
	class LuaHandler final : public ConfigParser {
		LbConfigParser &parent;
		LbLuaHandlerConfig config;

	public:
		LuaHandler(LbConfigParser &_parent, const char *_name)
			:parent(_parent), config(_name) {}

	protected:
		/* virtual methods from class ConfigParser */
		void ParseLine(FileLineParser &line) override;
		void Finish() override;
	};
#endif // HAVE_LUA

	class TranslationHandler final : public ConfigParser {
		LbConfigParser &parent;
		LbTranslationHandlerConfig config;

	public:
		TranslationHandler(LbConfigParser &_parent, const char *_name)
			:parent(_parent), config(_name) {
			config.address.Clear();
		}

	protected:
		/* virtual methods from class ConfigParser */
		void ParseLine(FileLineParser &line) override;
		void Finish() override;
	};

	class PrometheusExporter final : public ConfigParser {
		LbConfigParser &parent;
		LbPrometheusExporterConfig config;

	public:
		PrometheusExporter(LbConfigParser &_parent, const char *_name)
			:parent(_parent), config(_name) {}

	protected:
		/* virtual methods from class ConfigParser */
		void ParseLine(FileLineParser &line) override;
		void Finish() override;
	};

#ifdef HAVE_AVAHI
	class PrometheusDiscovery final : public ConfigParser {
		LbConfigParser &parent;
		LbPrometheusDiscoveryConfig config;

	public:
		PrometheusDiscovery(LbConfigParser &_parent, const char *_name)
			:parent(_parent), config(_name) {}

	protected:
		/* virtual methods from class ConfigParser */
		void ParseLine(FileLineParser &line) override;
		void Finish() override;
	};
#endif // HAVE_AVAHI

	class Listener final : public ConfigParser {
		LbConfigParser &parent;
		LbListenerConfig config;

	public:
		Listener(LbConfigParser &_parent, const char *_name)
			:parent(_parent), config(_name) {}

	protected:
		/* virtual methods from class ConfigParser */
		void ParseLine(FileLineParser &line) override;
		void Finish() override;
	};

	class GlobalHttpCheck final : public ConfigParser {
		LbConfigParser &parent;
		LbHttpCheckConfig config;

	public:
		explicit GlobalHttpCheck(LbConfigParser &_parent)
			:parent(_parent) {}

	protected:
		/* virtual methods from class ConfigParser */
		void ParseLine(FileLineParser &line) override;
		void Finish() override;
	};

public:
	explicit LbConfigParser(LbConfig &_config)
		:config(_config) {}

protected:
	/* virtual methods from class NestedConfigParser */
	void ParseLine2(FileLineParser &line) override;
	void FinishChild(std::unique_ptr<ConfigParser> &&child) override;

private:
	void CreateControl(FileLineParser &line);
	void CreateCertDatabase(FileLineParser &line);
	void CreateMonitor(FileLineParser &line);
	void CreateNode(FileLineParser &line);

	LbNodeConfig &AutoCreateNode(const char *name);
	void AutoCreateMember(LbMemberConfig &member, const char *name);

	void CreateCluster(FileLineParser &line);
	void CreateBranch(FileLineParser &line);
#ifdef HAVE_LUA
	void CreateLuaHandler(FileLineParser &line);
#endif // HAVE_LUA
	void CreateTranslationHandler(FileLineParser &line);
	void CreatePrometheusExporter(FileLineParser &line);
	void CreatePrometheusDiscovery(FileLineParser &line);
	void CreateListener(FileLineParser &line);
	void CreateGlobalHttpCheck(FileLineParser &line);
};

void
LbConfigParser::Control::ParseLine(FileLineParser &line)
{
	const char *word = line.ExpectWord();

	if (StringIsEqual(word, "bind")) {
		config.bind_address = ParseSocketAddress(line.ExpectValueAndEnd(),
							 BengControl::DEFAULT_PORT, true);
	} else if (StringIsEqual(word, "multicast_group")) {
		config.multicast_group = ParseSocketAddress(line.ExpectValueAndEnd(),
							    0, false);
	} else if (StringIsEqual(word, "interface")) {
		config.interface = line.ExpectValueAndEnd();
	} else
		throw LineParser::Error("Unknown option");
}

void
LbConfigParser::Control::Finish()
{
	if (config.bind_address.IsNull())
		throw LineParser::Error("Bind address is missing");

	config.Fixup();

	parent.config.controls.emplace_back(std::move(config));

	ConfigParser::Finish();
}

inline void
LbConfigParser::CreateControl(FileLineParser &line)
{
	line.ExpectSymbolAndEol('{');
	SetChild(std::make_unique<Control>(*this));
}

inline void
LbConfigParser::CreateGlobalHttpCheck(FileLineParser &line)
{
	line.ExpectSymbolAndEol('{');

	if (config.global_http_check)
		throw LineParser::Error("'global_http_check' already configured");

	SetChild(std::make_unique<GlobalHttpCheck>(*this));
}

void
LbConfigParser::CertDatabase::ParseLine(FileLineParser &line)
{
	const char *word = line.ExpectWord();

	if (config.ParseLine(word, line)) {
	} else if (StringIsEqual(word, "ca_cert")) {
		config.ca_certs.emplace_back(line.ExpectValueAndEnd());
	} else
		throw std::runtime_error("Unknown option");
}

void
LbConfigParser::CertDatabase::Finish()
{
	config.Check();

	auto i = parent.config.cert_dbs.emplace(std::string(config.name),
						std::move(config));
	if (!i.second)
		throw LineParser::Error("Duplicate certdb name");

	ConfigParser::Finish();
}

inline void
LbConfigParser::CreateCertDatabase(FileLineParser &line)
{
	const char *name = line.ExpectValue();
	line.ExpectSymbolAndEol('{');

	SetChild(std::make_unique<CertDatabase>(*this, name));
}

void
LbConfigParser::Monitor::ParseLine(FileLineParser &line)
{
	const char *word = line.ExpectWord();

	if (StringIsEqual(word, "type")) {
		if (config.type != LbMonitorConfig::Type::NONE)
			throw LineParser::Error("Monitor type already specified");

		const char *value = line.ExpectValueAndEnd();
		if (StringIsEqual(value, "none"))
			config.type = LbMonitorConfig::Type::NONE;
		else if (StringIsEqual(value, "ping"))
			config.type = LbMonitorConfig::Type::PING;
		else if (StringIsEqual(value, "connect"))
			config.type = LbMonitorConfig::Type::CONNECT;
		else if (StringIsEqual(value, "tcp_expect"))
			config.type = LbMonitorConfig::Type::TCP_EXPECT;
		else
			throw LineParser::Error("Unknown monitor type");
	} else if (StringIsEqual(word, "interval")) {
		config.interval = std::chrono::seconds(line.NextPositiveInteger());
	} else if (StringIsEqual(word, "timeout")) {
		config.timeout = std::chrono::seconds(line.NextPositiveInteger());
	} else if (config.type == LbMonitorConfig::Type::TCP_EXPECT &&
		   StringIsEqual(word, "connect_timeout")) {
		config.connect_timeout = std::chrono::seconds(line.NextPositiveInteger());
	} else if (config.type == LbMonitorConfig::Type::TCP_EXPECT &&
		   StringIsEqual(word, "send")) {
		const char *value = line.NextUnescape();
		if (value == nullptr)
			throw LineParser::Error("String value expected");

		line.ExpectEnd();

		config.send = value;
	} else if (config.type == LbMonitorConfig::Type::TCP_EXPECT &&
		   StringIsEqual(word, "expect")) {
		const char *value = line.NextUnescape();
		if (value == nullptr)
			throw LineParser::Error("String value expected");

		line.ExpectEnd();

		config.expect = value;
	} else if (config.type == LbMonitorConfig::Type::TCP_EXPECT &&
		   StringIsEqual(word, "expect_graceful")) {
		const char *value = line.NextUnescape();
		if (value == nullptr)
			throw LineParser::Error("String value expected");

		line.ExpectEnd();

		config.fade_expect = value;
	} else
		throw LineParser::Error("Unknown option");
}

void
LbConfigParser::Monitor::Finish()
{
	if (config.type == LbMonitorConfig::Type::TCP_EXPECT &&
	    (config.expect.empty() && config.fade_expect.empty()))
		throw LineParser::Error("No 'expect' string configured");

	auto i = parent.config.monitors.emplace(std::string(config.name),
						std::move(config));
	if (!i.second)
		throw LineParser::Error("Duplicate monitor name");

	ConfigParser::Finish();
}

inline void
LbConfigParser::CreateMonitor(FileLineParser &line)
{
	const char *name = line.ExpectValue();
	line.ExpectSymbolAndEol('{');

	SetChild(std::make_unique<Monitor>(*this, name));
}

void
LbConfigParser::Node::ParseLine(FileLineParser &line)
{
	const char *word = line.ExpectWord();

	if (StringIsEqual(word, "address")) {
		if (!config.address.IsNull())
			throw LineParser::Error("Duplicate node address");

		const char *value = line.ExpectValueAndEnd();

		config.address = ParseSocketAddress(value, 0, false);
	} else if (StringIsEqual(word, "jvm_route")) {
		if (!config.jvm_route.empty())
			throw LineParser::Error("Duplicate jvm_route");

		config.jvm_route = line.ExpectValueAndEnd();
	} else
		throw LineParser::Error("Unknown option");
}

void
LbConfigParser::Node::Finish()
{
	if (config.address.IsNull())
		config.address = ParseSocketAddress(config.name.c_str(), 0, false);

	auto i = parent.config.nodes.emplace(std::string(config.name),
					     std::move(config));
	if (!i.second)
		throw LineParser::Error("Duplicate node name");

	ConfigParser::Finish();
}

inline void
LbConfigParser::CreateNode(FileLineParser &line)
{
	const char *name = line.ExpectValue();
	line.ExpectSymbolAndEol('{');

	SetChild(std::make_unique<Node>(*this, name));
}

LbNodeConfig &
LbConfigParser::AutoCreateNode(const char *name)
{
	auto address = ParseSocketAddress(name, 0, false);

	auto i = config.nodes.insert(std::make_pair(name,
						    LbNodeConfig(name,
								 std::move(address))));
	return i.first->second;
}

void
LbConfigParser::AutoCreateMember(LbMemberConfig &member, const char *name)
{
	member.node = &AutoCreateNode(name);
	member.port = 0;
}

static unsigned
parse_port(const char *p, SocketAddress address)
{
	const auto hints = MakeAddrInfo(0, address.GetFamily(), SOCK_STREAM);

	struct addrinfo *ai;
	if (getaddrinfo(nullptr, p, &hints, &ai) != 0)
		return 0;

	unsigned port = SocketAddress(ai->ai_addr, ai->ai_addrlen).GetPort();
	freeaddrinfo(ai);
	return port;
}

[[gnu::pure]]
static bool
validate_protocol_sticky(LbProtocol protocol, StickyMode sticky)
{
	switch (protocol) {
	case LbProtocol::HTTP:
		return true;

	case LbProtocol::TCP:
		switch (sticky) {
		case StickyMode::NONE:
		case StickyMode::FAILOVER:
		case StickyMode::SOURCE_IP:
			return true;

		case StickyMode::HOST:
		case StickyMode::XHOST:
		case StickyMode::SESSION_MODULO:
		case StickyMode::COOKIE:
		case StickyMode::JVM_ROUTE:
			return false;
		}
	}

	return false;
}

#ifdef HAVE_AVAHI

[[gnu::const]]
static bool
ValidateZeroconfSticky(StickyMode sticky) noexcept
{
	switch (sticky) {
	case StickyMode::NONE:
	case StickyMode::FAILOVER:
	case StickyMode::SOURCE_IP:
	case StickyMode::HOST:
	case StickyMode::XHOST:
		return true;

	case StickyMode::SESSION_MODULO:
	case StickyMode::COOKIE:
	case StickyMode::JVM_ROUTE:
		return false;
	}

	std::unreachable();
}

#endif

[[gnu::pure]]
static StickyMode
ParseStickyMode(const char *s)
{
	if (StringIsEqual(s, "none"))
		return StickyMode::NONE;
	else if (StringIsEqual(s, "failover"))
		return StickyMode::FAILOVER;
	else if (StringIsEqual(s, "source_ip"))
		return StickyMode::SOURCE_IP;
	else if (StringIsEqual(s, "host"))
		return StickyMode::HOST;
	else if (StringIsEqual(s, "xhost"))
		return StickyMode::XHOST;
	else if (StringIsEqual(s, "session_modulo"))
		return StickyMode::SESSION_MODULO;
	else if (StringIsEqual(s, "cookie"))
		return StickyMode::COOKIE;
	else if (StringIsEqual(s, "jvm_route"))
		return StickyMode::JVM_ROUTE;
	else
		throw LineParser::Error("Unknown sticky mode");
}

#ifdef HAVE_AVAHI

[[gnu::pure]]
static LbClusterConfig::StickyMethod
ParseStickyMethod(const char *s)
{
	if (StringIsEqual(s, "consistent_hashing"))
		return LbClusterConfig::StickyMethod::CONSISTENT_HASHING;
	else if (StringIsEqual(s, "rendezvous_hashing"))
		return LbClusterConfig::StickyMethod::RENDEZVOUS_HASHING;
	else if (StringIsEqual(s, "cache"))
		return LbClusterConfig::StickyMethod::CACHE;
	else
		throw LineParser::Error("Unknown sticky method");
}

#endif

void
LbConfigParser::Cluster::ParseLine(FileLineParser &line)
{
	const char *word = line.ExpectWord();

	if (StringIsEqual(word, "name")) {
		config.name = line.ExpectValueAndEnd();
	} else if (StringIsEqual(word, "http_host")) {
		config.http_host = line.ExpectValueAndEnd();
		if (!VerifyDomainName(config.http_host))
			throw LineParser::Error("Invalid domain name");
	} else if (StringIsEqual(word, "sticky")) {
		config.sticky_mode = ParseStickyMode(line.ExpectValueAndEnd());
	} else if (StringIsEqual(word, "sticky_method")) {
#ifdef HAVE_AVAHI
		config.sticky_method = ParseStickyMethod(line.ExpectValueAndEnd());
#else
		throw LineParser::Error("Zeroconf support is disabled at compile time");
#endif
	} else if (StringIsEqual(word, "sticky_cache")) {
		// deprecated since 18.0.29, use "sticky_method" instead
#ifdef HAVE_AVAHI
		config.sticky_method = line.NextBool()
			? LbClusterConfig::StickyMethod::CACHE
			: LbClusterConfig::StickyMethod::CONSISTENT_HASHING;
		line.ExpectEnd();
#else
		throw LineParser::Error("Zeroconf support is disabled at compile time");
#endif
	} else if (StringIsEqual(word, "session_cookie")) {
		config.session_cookie = line.ExpectValueAndEnd();
	} else if (StringIsEqual(word, "monitor")) {
		if (config.monitor != nullptr)
			throw LineParser::Error("Monitor already specified");

		config.monitor = parent.config.FindMonitor(line.ExpectValueAndEnd());
		if (config.monitor == nullptr)
			throw LineParser::Error("No such monitor");
	} else if (StringIsEqual(word, "member")) {
#ifdef HAVE_AVAHI
		if (config.zeroconf.IsEnabled())
			throw LineParser::Error("Cannot configure both hard-coded members and Zeroconf");
#endif

		char *name = line.ExpectValue();

		/*
		  line.ExpectEnd();
		*/

		config.members.emplace_back();

		auto *member = &config.members.back();

		member->node = parent.config.FindNode(name);
		if (member->node == nullptr) {
			char *q = strchr(name, ':');
			if (q != nullptr) {
				*q++ = 0;
				member->node = parent.config.FindNode(name);
				if (member->node == nullptr) {
					/* node doesn't exist: parse the given member
					   name, auto-create a new node */

					/* restore the colon */
					*--q = ':';

					parent.AutoCreateMember(*member, name);
					return;
				}

				member->port = parse_port(q, member->node->address);
				if (member->port == 0)
					throw LineParser::Error("Malformed port");
			} else
				/* node doesn't exist: parse the given member
				   name, auto-create a new node */
				parent.AutoCreateMember(*member, name);
		}
#ifdef HAVE_AVAHI
	} else if (config.zeroconf.ParseLine(word, line)) {
		if (!config.members.empty())
			throw LineParser::Error("Cannot configure both hard-coded members and Zeroconf");
#else
	} else if (StringStartsWith(word, "zeroconf_")) {
		throw LineParser::Error("Zeroconf support is disabled at compile time");
#endif
	} else if (StringIsEqual(word, "protocol")) {
		const char *protocol = line.ExpectValueAndEnd();
		if (StringIsEqual(protocol, "http"))
			config.protocol = LbProtocol::HTTP;
		else if (StringIsEqual(protocol, "tcp"))
			config.protocol = LbProtocol::TCP;
		else
			throw LineParser::Error("Unknown protocol");
	} else if (StringIsEqual(word, "ssl")) {
		const bool value = line.NextBool();
		line.ExpectEnd();

		if (config.ssl && !value)
			throw LineParser::Error{"SSL cannot be disabled at this point"};

		config.ssl = value;
	} else if (StringIsEqual(word, "hsts")) {
		config.hsts = line.NextBool();
		line.ExpectEnd();
	} else if (StringIsEqual(word, "fair_scheduling")) {
		config.fair_scheduling = line.NextBool();
		line.ExpectEnd();
	} else if (StringIsEqual(word, "tarpit")) {
		config.tarpit = line.NextBool();
		line.ExpectEnd();
	} else if (StringIsEqual(word, "source_address")) {
		const char *address = line.ExpectValueAndEnd();
		if (strcmp(address, "transparent") != 0)
			throw LineParser::Error("\"transparent\" expected");

		config.transparent_source = true;
	} else if (StringIsEqual(word, "mangle_via")) {
		config.mangle_via = line.NextBool();

		line.ExpectEnd();
	} else if (StringIsEqual(word, "fallback")) {
		if (config.fallback.IsDefined())
			throw LineParser::Error("Duplicate fallback");

		const char *location = line.ExpectValue();
		if (strstr(location, "://") != nullptr) {
			line.ExpectEnd();

			config.fallback.status = HttpStatus::FOUND;
			config.fallback.location = location;
		} else {
			char *endptr;
			HttpStatus status =
				static_cast<HttpStatus>(strtoul(location, &endptr, 10));
			if (*endptr != 0 || !http_status_is_valid(status))
				throw LineParser::Error("Invalid HTTP status code");

			if (http_status_is_empty(status))
				throw LineParser::Error("This HTTP status does not allow a response body");

			const char *message = line.ExpectValue();

			line.ExpectEnd();

			config.fallback.status = status;
			config.fallback.message = message;
		}
	} else
		throw LineParser::Error("Unknown option");
}

void
LbConfigParser::Cluster::Finish()
{
	if (config.monitor != nullptr && !config.monitor->IsDefined())
		/* if the monitor is disabled, it's pointless to keep a
		   pointer to it */
		config.monitor = nullptr;

#ifdef HAVE_AVAHI
	config.zeroconf.Check();
#endif

	if (config.members.empty() && !config.HasZeroConf())
		throw LineParser::Error("Pool has no members");

	if (NeedsPort(config.protocol) && config.GetDefaultPort() == 0) {
		/* this protocol has no default port - all members
		   must have a port */
		for (const auto &i : config.members) {
			if (i.IsPortMissing())
				throw LineParser::Error{"No port on member"};
		}
	}

	if (!validate_protocol_sticky(config.protocol, config.sticky_mode))
		throw LineParser::Error("The selected sticky mode not available for this protocol");

	if (config.protocol != LbProtocol::HTTP && config.ssl)
		throw LineParser::Error{"SSL/TLS only available with HTTP"};

#ifdef HAVE_AVAHI
	if (config.HasZeroConf() &&
	    !ValidateZeroconfSticky(config.sticky_mode))
		throw LineParser::Error("The selected sticky mode not compatible with Zeroconf");
#endif

	if (config.members.size() == 1)
		/* with only one member, a sticky setting doesn't make
		   sense */
		config.sticky_mode = StickyMode::NONE;

	auto i = parent.config.clusters.emplace(std::string(config.name),
						std::move(config));
	if (!i.second)
		throw LineParser::Error("Duplicate pool name");

	i.first->second.FillAddressList();

	ConfigParser::Finish();
}

inline void
LbConfigParser::CreateCluster(FileLineParser &line)
{
	const char *name = line.ExpectValue();
	line.ExpectSymbolAndEol('{');

	SetChild(std::make_unique<Cluster>(*this, name));
}

static LbAttributeReference
ParseAttributeReference(const char *p)
{
	if (StringIsEqual(p, "request_method")) {
		return LbAttributeReference::Type::METHOD;
	} else if (StringIsEqual(p, "request_uri")) {
		return LbAttributeReference::Type::URI;
	} else if (StringIsEqual(p, "remote_address")) {
		return LbAttributeReference::Type::REMOTE_ADDRESS;
	} else if (StringIsEqual(p, "peer_subject")) {
		return LbAttributeReference::Type::PEER_SUBJECT;
	} else if (StringIsEqual(p, "peer_issuer_subject")) {
		return LbAttributeReference::Type::PEER_ISSUER_SUBJECT;
	} else if (auto header = StringAfterPrefix(p, "http_")) {
		LbAttributeReference a(LbAttributeReference::Type::HEADER, header);
		if (a.name.empty())
			throw LineParser::Error("Empty HTTP header name");

		for (auto &ch : a.name) {
			if (ch == '_')
				ch = '-';
			else if (!IsLowerAlphaASCII(ch) && !IsDigitASCII(ch))
				throw LineParser::Error("Malformed HTTP header name");
		}

		return a;
	} else
		throw LineParser::Error("Unknown attribute reference");
}

static LbConditionConfig
ParseCondition(FileLineParser &line)
{
	if (!line.SkipSymbol('$'))
		throw LineParser::Error("Attribute name starting with '$' expected");

	const char *attribute = line.NextWord();
	if (attribute == nullptr)
		throw LineParser::Error("Attribute name starting with '$' expected");

	auto a = ParseAttributeReference(attribute);

	if (a.IsAddress()) {
		bool negate = false;

		const char *in = line.NextWord();
		if (in != nullptr && StringIsEqual(in, "not")) {
			negate = true;
			in = line.NextWord();
		}

		if (in == nullptr || !StringIsEqual(in, "in"))
			throw LineParser::Error("'in' expected");

		const char *s = line.NextValue();
		if (s == nullptr)
			throw LineParser::Error("Value expected");

		return {std::move(a), negate, MaskedSocketAddress{s}};
	}

	bool re, negate;

	if (line.SkipSymbol('=', '=')) {
		re = false;
		negate = false;
	} else if (line.SkipSymbol('!', '=')) {
		re = false;
		negate = true;
	} else if (line.SkipSymbol('=', '~')) {
		re = true;
		negate = false;
	} else if (line.SkipSymbol('!', '~')) {
		re = true;
		negate = true;
	} else
		throw LineParser::Error("Comparison operator expected");

	line.ExpectWhitespace();

	const char *string = line.NextUnescape();
	if (string == nullptr)
		throw LineParser::Error("Regular expression expected");

	if (re)
		return {std::move(a), negate, UniqueRegex{string, {}}};
	else
		return {std::move(a), negate, string};
}

static HttpStatus
ParseStatus(const char *s)
{
	char *endptr;
	auto l = strtoul(s, &endptr, 10);
	if (endptr == s || *endptr != 0)
		throw LineParser::Error("Failed to parse status number");

	auto status = static_cast<HttpStatus>(l);
	if (l < 200 || l >= 600 || !http_status_is_valid(status))
		throw LineParser::Error("Invalid status");

	return status;
}

void
LbConfigParser::Branch::AddGoto(LbGotoConfig &&destination,
				FileLineParser &line)
{
	if (line.IsEnd()) {
		if (config.HasFallback())
			throw LineParser::Error("Fallback already specified");

		if (!config.conditions.empty() &&
		    config.conditions.front().destination.GetProtocol() != destination.GetProtocol())
			throw LineParser::Error("Protocol mismatch");

		config.fallback = std::move(destination);
	} else {
		if (config.fallback.IsDefined() &&
		    config.fallback.GetProtocol() != destination.GetProtocol())
			throw LineParser::Error("Protocol mismatch");

		const char *if_ = line.NextWord();
		if (if_ == nullptr || strcmp(if_, "if") != 0)
			throw LineParser::Error("'if' or end of line expected");

		auto condition = ParseCondition(line);

		line.ExpectEnd();

		config.conditions.emplace_back(std::move(condition),
					       std::move(destination));
	}
}

void
LbConfigParser::Branch::ParseLine(FileLineParser &line)
{
	const char *word = line.ExpectWord();

	if (StringIsEqual(word, "goto")) {
		const char *name = line.ExpectValue();

		LbGotoConfig destination = parent.config.FindGoto(name);
		if (!destination.IsDefined())
			throw LineParser::Error("No such pool");

		AddGoto(std::move(destination), line);
	} else if (StringIsEqual(word, "status")) {
		const auto status = ParseStatus(line.ExpectValue());
		LbGotoConfig destination(status);

		AddGoto(std::move(destination), line);
	} else if (StringIsEqual(word, "redirect")) {
		LbGotoConfig destination(HttpStatus::FOUND);
		std::get<LbSimpleHttpResponse>(destination.destination).location = line.ExpectValue();

		AddGoto(std::move(destination), line);
	} else if (StringIsEqual(word, "redirect_https")) {
		const bool value = line.NextBool();
		if (!value)
			throw LineParser::Error("Invalid value");

		LbGotoConfig destination(HttpStatus::MOVED_PERMANENTLY);
		std::get<LbSimpleHttpResponse>(destination.destination).redirect_https = true;
		AddGoto(std::move(destination), line);
	} else
		throw LineParser::Error("Unknown option");
}

void
LbConfigParser::Branch::Finish()
{
	if (!config.HasFallback())
		throw LineParser::Error("Branch has no fallback");

	if (config.GetProtocol() != LbProtocol::HTTP)
		throw LineParser::Error("Only HTTP pools allowed in branch");

	auto i = parent.config.branches.emplace(std::string(config.name),
						std::move(config));
	if (!i.second)
		throw LineParser::Error("Duplicate pool/branch name");

	ConfigParser::Finish();
}

inline void
LbConfigParser::CreateBranch(FileLineParser &line)
{
	const char *name = line.ExpectValue();
	line.ExpectSymbolAndEol('{');

	SetChild(std::make_unique<Branch>(*this, name));
}

#ifdef HAVE_LUA

void
LbConfigParser::LuaHandler::ParseLine(FileLineParser &line)
{
	const char *word = line.ExpectWord();

	if (StringIsEqual(word, "path")) {
		if (!config.path.empty())
			throw LineParser::Error("Duplicate 'path'");

		config.path = line.ExpectPathAndEnd();
	} else if (StringIsEqual(word, "function")) {
		if (!config.function.empty())
			throw LineParser::Error("Duplicate 'function'");

		config.function = line.ExpectValueAndEnd();
	} else
		throw LineParser::Error("Unknown option");
}

void
LbConfigParser::LuaHandler::Finish()
{
	if (config.path.empty())
		throw LineParser::Error("lua_handler has no 'path'");

	if (config.function.empty())
		throw LineParser::Error("lua_handler has no 'function'");

	auto i = parent.config.lua_handlers.emplace(std::string(config.name),
						    std::move(config));
	if (!i.second)
		throw LineParser::Error("Duplicate pool/branch name");

	ConfigParser::Finish();
}

inline void
LbConfigParser::CreateLuaHandler(FileLineParser &line)
{
	const char *name = line.ExpectValue();
	line.ExpectSymbolAndEol('{');

	SetChild(std::make_unique<LuaHandler>(*this, name));
}

#endif // HAVE_LUA

void
LbConfigParser::TranslationHandler::ParseLine(FileLineParser &line)
{
	const char *word = line.ExpectWord();

	if (StringIsEqual(word, "connect")) {
		if (config.address.IsDefined())
			throw LineParser::Error("Duplicate 'connect'");

		config.address.SetLocal(line.ExpectValueAndEnd());
	} else if (StringIsEqual(word, "pools")) {
		while (!line.IsEnd()) {
			const char *name = line.ExpectValue();
			const auto destination = parent.config.FindGoto(name);
			if (!destination.IsDefined())
				throw FmtRuntimeError("No such pool: {}", name);

			if (destination.GetProtocol() != LbProtocol::HTTP)
				throw LineParser::Error("Only HTTP pools allowed");

			assert(destination.GetName() != nullptr);
			assert(StringIsEqual(destination.GetName(), name));

			auto i = config.destinations.emplace(destination.GetName(),
							     std::move(destination));
			if (!i.second)
				throw FmtRuntimeError("Duplicate pool: {}", name);
		}
	} else
		throw LineParser::Error("Unknown option");
}

void
LbConfigParser::TranslationHandler::Finish()
{
	if (!config.address.IsDefined())
		throw LineParser::Error("translation_handler has no 'connect'");

	if (config.destinations.empty())
		throw LineParser::Error("translation_handler has no pools");

	auto i = parent.config.translation_handlers.emplace(std::string(config.name),
							    std::move(config));
	if (!i.second)
		throw LineParser::Error("Duplicate translation_handler name");

	ConfigParser::Finish();
}

void
LbConfigParser::PrometheusExporter::ParseLine(FileLineParser &line)
{
	const char *word = line.ExpectWord();

	if (StringIsEqual(word, "load_from_local")) {
		config.load_from_local.emplace_front(ParseSocketAddress(line.ExpectValueAndEnd(),
									80, false));
	} else
		throw LineParser::Error("Unknown option");
}

void
LbConfigParser::PrometheusExporter::Finish()
{
	auto i = parent.config.prometheus_exporters.emplace(std::string(config.name),
							    std::move(config));
	if (!i.second)
		throw LineParser::Error("Duplicate prometheus_exporter name");

	ConfigParser::Finish();
}

inline void
LbConfigParser::CreatePrometheusExporter(FileLineParser &line)
{
	const char *name = line.ExpectValue();
	line.ExpectSymbolAndEol('{');

	SetChild(std::make_unique<PrometheusExporter>(*this, name));
}

#ifdef HAVE_AVAHI

void
LbConfigParser::PrometheusDiscovery::ParseLine(FileLineParser &line)
{
	const char *word = line.ExpectWord();

	if (!config.zeroconf.ParseLine(word, line))
		throw LineParser::Error("Unknown option");
}

void
LbConfigParser::PrometheusDiscovery::Finish()
{
	if (!config.zeroconf.IsEnabled())
		throw LineParser::Error("Missing zeroconf_service");

	config.zeroconf.Check();

	auto i = parent.config.prometheus_discoveries.emplace(std::string(config.name),
							      std::move(config));
	if (!i.second)
		throw LineParser::Error("Duplicate prometheus_discovery name");

	ConfigParser::Finish();
}

#endif // HAVE_AVAHI

inline void
LbConfigParser::CreatePrometheusDiscovery(FileLineParser &line)
{
#ifdef HAVE_AVAHI
	const char *name = line.ExpectValue();
	line.ExpectSymbolAndEol('{');

	SetChild(std::make_unique<PrometheusDiscovery>(*this, name));
#else // HAVE_AVAHI
	(void)line;
	throw LineParser::Error("Zeroconf support is disabled at compile time");
#endif // HAVE_AVAHI
}

inline void
LbConfigParser::CreateTranslationHandler(FileLineParser &line)
{
	const char *name = line.ExpectValue();
	line.ExpectSymbolAndEol('{');

	SetChild(std::make_unique<TranslationHandler>(*this, name));
}

void
LbConfigParser::Listener::ParseLine(FileLineParser &line)
{
	const char *word = line.ExpectWord();

	if (StringIsEqual(word, "bind")) {
		const char *address = line.ExpectValueAndEnd();

		config.bind_address = ParseSocketAddress(address, 80, true);
	} else if (StringIsEqual(word, "interface")) {
		config.interface = line.ExpectValueAndEnd();
	} else if (StringIsEqual(word, "mode")) {
		if (config.bind_address.IsNull() ||
		    config.bind_address.GetFamily() != AF_LOCAL)
			throw LineParser::Error("'mode' works only with local sockets");

		const char *s = line.ExpectValueAndEnd();
		char *endptr;
		const unsigned long value = strtoul(s, &endptr, 8);
		if (endptr == s || *endptr != 0)
			throw LineParser::Error("Not a valid octal value");

		if (value & ~0777ULL)
			throw LineParser::Error("Not a valid mode");

		config.mode = value;
	} else if (StringIsEqual(word, "mptcp")) {
		config.mptcp = line.NextBool();
		line.ExpectEnd();
	} else if (StringIsEqual(word, "tag")) {
		config.tag = line.ExpectValueAndEnd();
	} else if (StringIsEqual(word, "zeroconf_service")) {
#ifdef HAVE_AVAHI
		config.zeroconf_service = MakeZeroconfServiceType(line.ExpectValueAndEnd(),
								  "_tcp");
#else
		throw LineParser::Error("Zeroconf support is disabled at compile time");
#endif
	} else if (StringIsEqual(word, "zeroconf_interface")) {
#ifdef HAVE_AVAHI
		if (config.zeroconf_service.empty())
			throw LineParser::Error("zeroconf_interface without zeroconf_service");

		if (!config.zeroconf_interface.empty())
			throw LineParser::Error("Duplicate zeroconf_interface");

		config.zeroconf_interface = line.ExpectValueAndEnd();
#else
		throw LineParser::Error("Zeroconf support is disabled at compile time");
#endif
	} else if (StringIsEqual(word, "max_connections_per_ip")) {
		config.max_connections_per_ip = line.NextPositiveInteger();
		line.ExpectEnd();
	} else if (StringIsEqual(word, "ack_timeout")) {
		config.tcp_user_timeout = line.NextPositiveInteger() * 1000;
		line.ExpectEnd();
	} else if (StringIsEqual(word, "keepalive")) {
		config.keepalive = line.NextBool();
		line.ExpectEnd();
	} else if (StringIsEqual(word, "v6only")) {
		config.v6only = line.NextBool();
		line.ExpectEnd();
	} else if (StringIsEqual(word, "reuse_port")) {
		config.reuse_port = line.NextBool();
		line.ExpectEnd();
	} else if (StringIsEqual(word, "free_bind")) {
		config.free_bind = line.NextBool();
		line.ExpectEnd();
	} else if (StringIsEqual(word, "pool")) {
		if (config.destination.IsDefined())
			throw LineParser::Error("Pool already configured");

		config.destination = parent.config.FindGoto(line.ExpectValueAndEnd());
		if (!config.destination.IsDefined())
			throw LineParser::Error("No such pool");
	} else if (StringIsEqual(word, "redirect_https")) {
		const bool value = line.NextBool();
		line.ExpectEnd();

		if (config.destination.IsDefined())
			throw LineParser::Error("Pool already configured");

		if (!value)
			return;

		config.destination = LbGotoConfig{HttpStatus::MOVED_PERMANENTLY};
		std::get<LbSimpleHttpResponse>(config.destination.destination).redirect_https = true;
	} else if (StringIsEqual(word, "access_logger")) {
		const char *value = line.ExpectValueAndEnd();

		if (StringIsEqual(value, "yes"))
			config.access_logger = true;
		else if (StringIsEqual(value, "no"))
			config.access_logger = false;
		else {
			config.access_logger_name = value;

			if (parent.config.access_log.named.find(config.access_logger_name) == parent.config.access_log.named.end())
				throw LineParser::Error("No such access_logger");
		}
	} else if (StringIsEqual(word, "access_logger_only_errors")) {
		config.access_logger_only_errors = line.NextBool();
		line.ExpectEnd();
	} else if (StringIsEqual(word, "verbose_response")) {
		bool value = line.NextBool();

		line.ExpectEnd();

		config.verbose_response = value;
	} else if (StringIsEqual(word, "force_http2")) {
#ifdef HAVE_NGHTTP2
		config.force_http2 = line.NextBool();
#endif
	} else if (StringIsEqual(word, "alpn_http2")) {
#ifdef HAVE_NGHTTP2
		config.alpn_http2 = line.NextBool();
#endif
	} else if (StringIsEqual(word, "ssl")) {
		bool value = line.NextBool();

		if (config.ssl && !value)
			throw LineParser::Error("SSL cannot be disabled at this point");

		line.ExpectEnd();

		config.ssl = value;
	} else if (StringIsEqual(word, "ssl_cert_db")) {
		if (!config.ssl)
			throw LineParser::Error("SSL is not enabled");

		if (config.cert_db != nullptr)
			throw LineParser::Error("ssl_cert_db already set");

		const char *name = line.ExpectValueAndEnd();
		config.cert_db = parent.config.FindCertDb(name);
		if (config.cert_db == nullptr)
			throw LineParser::Error(std::string("No such cert_db: ") + name);
	} else if (StringIsEqual(word, "ssl_cert")) {
		if (!config.ssl)
			throw LineParser::Error("SSL is not enabled");

		const char *path = line.ExpectValue();

		const char *key_path = nullptr;
		if (!line.IsEnd())
			key_path = line.ExpectValue();

		line.ExpectEnd();

		auto &cks = config.ssl_config.cert_key;
		if (!cks.empty()) {
			auto &front = cks.front();

			if (key_path == nullptr) {
				if (front.cert_file.empty()) {
					front.cert_file = path;
					return;
				} else
					throw LineParser::Error("Certificate already configured");
			} else {
				if (front.cert_file.empty())
					throw LineParser::Error("Previous certificate missing");
				if (front.key_file.empty())
					throw LineParser::Error("Previous key missing");
			}
		}

		if (key_path == nullptr)
			key_path = "";

		cks.emplace_back(path, key_path);
	} else if (StringIsEqual(word, "ssl_key")) {
		if (!config.ssl)
			throw LineParser::Error("SSL is not enabled");

		const char *path = line.ExpectValueAndEnd();

		auto &cks = config.ssl_config.cert_key;
		if (!cks.empty()) {
			if (!cks.front().key_file.empty())
				throw LineParser::Error("Key already configured");

			cks.front().key_file = path;
		} else {
			cks.emplace_back(std::string(), path);
		}
	} else if (StringIsEqual(word, "ssl_ca_cert")) {
		if (!config.ssl)
			throw LineParser::Error("SSL is not enabled");

		if (!config.ssl_config.ca_cert_file.empty())
			throw LineParser::Error("Certificate already configured");

		config.ssl_config.ca_cert_file = line.ExpectValueAndEnd();
	} else if (StringIsEqual(word, "ssl_verify")) {
		if (!config.ssl)
			throw LineParser::Error("SSL is not enabled");

		const char *value = line.ExpectValueAndEnd();
		if (StringIsEqual(value, "yes"))
			config.ssl_config.verify = SslVerify::YES;
		else if (StringIsEqual(value, "no"))
			config.ssl_config.verify = SslVerify::NO;
		else if (StringIsEqual(value, "optional"))
			config.ssl_config.verify = SslVerify::OPTIONAL;
		else
			throw LineParser::Error("yes/no expected");
	} else if (StringIsEqual(word, "hsts")) {
		const bool value = line.NextBool();
		line.ExpectEnd();

		config.hsts = value;
	} else
		throw LineParser::Error("Unknown option");
}

void
LbConfigParser::Listener::Finish()
{
	if (parent.config.FindListener(config.name) != nullptr)
		throw LineParser::Error("Duplicate listener name");

	if (config.bind_address.IsNull())
		throw LineParser::Error("Listener has no destination");

	if (config.ssl && config.ssl_config.cert_key.empty())
		throw LineParser::Error("No SSL certificates ");

	if (const auto *response = std::get_if<LbSimpleHttpResponse>(&config.destination.destination))
		if (response->redirect_https && config.ssl)
			throw LineParser::Error("Cannot enable 'redirect_https' on HTTPS listener");

	if (config.destination.GetProtocol() == LbProtocol::HTTP ||
	    config.ssl)
		config.tcp_defer_accept = 10;

	if (config.hsts && config.destination.GetProtocol() != LbProtocol::HTTP)
		throw LineParser::Error{"HSTS only available with HTTP"};

	parent.config.listeners.emplace_back(std::move(config));

	ConfigParser::Finish();
}

void
LbConfigParser::GlobalHttpCheck::ParseLine(FileLineParser &line)
{
	const char *word = line.ExpectWord();

	if (StringIsEqual(word, "uri")) {
		if (!config.uri.empty())
			throw LineParser::Error("'uri' already specified");

		const char *value = line.ExpectValueAndEnd();
		if (*value != '/')
			throw LineParser::Error("'uri' must be an absolute URI path");

		config.uri = value;
	} else if (StringIsEqual(word, "host")) {
		if (!config.host.empty())
			throw LineParser::Error("'host' already specified");

		const char *value = line.ExpectValueAndEnd();
		if (*value == 0)
			throw LineParser::Error("'host' must not be empty");

		config.host = value;
	} else if (StringIsEqual(word, "client")) {
		const char *value = line.ExpectValueAndEnd();
		if (*value == 0)
			throw LineParser::Error("'client' must not be empty");

		config.client_addresses.emplace_front(value);
	} else if (StringIsEqual(word, "file_exists")) {
		if (!config.file_exists.empty())
			throw LineParser::Error("'file_exists' already specified");

		const char *value = line.ExpectValueAndEnd();
		if (*value != '/')
			throw LineParser::Error("'file_exists' must be an absolute path");

		config.file_exists = value;
	} else if (StringIsEqual(word, "success_message")) {
		if (!config.success_message.empty())
			throw LineParser::Error("'success_message' already specified");

		config.success_message = line.ExpectValueAndEnd();
	} else
		throw LineParser::Error("Unknown option");
}

void
LbConfigParser::GlobalHttpCheck::Finish()
{
	if (config.uri.empty())
		throw LineParser::Error("Missing 'uri'");

	if (config.host.empty())
		throw LineParser::Error("Missing 'host'");

	if (config.file_exists.empty())
		throw LineParser::Error("Missing 'file_exists'");

	parent.config.global_http_check = std::make_unique<LbHttpCheckConfig>(std::move(config));

	ConfigParser::Finish();
}

inline void
LbConfigParser::CreateListener(FileLineParser &line)
{
	const char *name = line.ExpectValue();
	line.ExpectSymbolAndEol('{');

	SetChild(std::make_unique<Listener>(*this, name));
}

void
LbConfigParser::ParseLine2(FileLineParser &line)
{
	const char *word = line.ExpectWord();

	if (StringIsEqual(word, "node"))
		CreateNode(line);
	else if (StringIsEqual(word, "pool"))
		CreateCluster(line);
	else if (StringIsEqual(word, "branch"))
		CreateBranch(line);
#ifdef HAVE_LUA
	else if (StringIsEqual(word, "lua_handler"))
		CreateLuaHandler(line);
#endif // HAVE_LUA
	else if (StringIsEqual(word, "translation_handler"))
		CreateTranslationHandler(line);
	else if (StringIsEqual(word, "prometheus_exporter"))
		CreatePrometheusExporter(line);
#ifdef HAVE_AVAHI
	else if (StringIsEqual(word, "prometheus_discovery"))
		CreatePrometheusDiscovery(line);
#endif // HAVE_AVAHI
	else if (StringIsEqual(word, "listener"))
		CreateListener(line);
	else if (StringIsEqual(word, "monitor"))
		CreateMonitor(line);
	else if (StringIsEqual(word, "cert_db"))
		CreateCertDatabase(line);
	else if (StringIsEqual(word, "control"))
		CreateControl(line);
	else if (StringIsEqual(word, "global_http_check"))
		CreateGlobalHttpCheck(line);
	else if (StringIsEqual(word, "access_logger")) {
		if (line.SkipSymbol('{')) {
			line.ExpectEnd();

			current_access_log = &config.access_log.main;
			SetChild(std::make_unique<AccessLogConfigParser>());
		} else {
			std::string name = line.ExpectValue();
			line.ExpectSymbolAndEol('{');

			auto [it, inserted] = config.access_log.named.try_emplace(std::move(name));
			if (!inserted)
				throw LineParser::Error{"An access_log with that name already exists"};

			current_access_log = &it->second;
			SetChild(std::make_unique<AccessLogConfigParser>());
		}
	} else if (StringIsEqual(word, "set")) {
		const char *name = line.ExpectWord();
		line.ExpectSymbol('=');
		const char *value = line.ExpectValueAndEnd();
		config.HandleSet(name, value);
	} else
		throw LineParser::Error("Unknown option");
}

void
LbConfigParser::FinishChild(std::unique_ptr<ConfigParser> &&c)
{
	if (auto *al = dynamic_cast<AccessLogConfigParser *>(c.get())) {
		*current_access_log = al->GetConfig();
	}
}

void
LoadConfigFile(LbConfig &config, const char *path)
{
	LbConfigParser parser(config);
	VariableConfigParser v_parser(parser);
	CommentConfigParser parser2(v_parser);
	IncludeConfigParser parser3(path, parser2);

	ParseConfigFile(path, parser3);
}
