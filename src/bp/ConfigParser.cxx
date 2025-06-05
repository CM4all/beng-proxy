// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <max.kellermann@ionos.com>

#include "Config.hxx"
#include "access_log/ConfigParser.hxx"
#include "spawn/ConfigParser.hxx"
#include "io/config/FileLineParser.hxx"
#include "io/config/ConfigParser.hxx"
#include "net/Parser.hxx"
#include "net/control/Protocol.hxx"
#include "util/StringAPI.hxx"

#ifdef HAVE_AVAHI
#include "lib/avahi/Check.hxx"
#endif

#include <string.h>

class BpConfigParser final : public NestedConfigParser {
	BpConfig &config;

	AccessLogConfig *current_access_log;

	class Listener final : public ConfigParser {
		BpConfigParser &parent;
		BpListenerConfig config;

	public:
		explicit Listener(BpConfigParser &_parent):parent(_parent) {}

	protected:
		/* virtual methods from class ConfigParser */
		void ParseLine(FileLineParser &line) override;
		void Finish() override;
	};

	class Control final : public ConfigParser {
		BpConfigParser &parent;
		BpConfig::ControlListener config;

	public:
		explicit Control(BpConfigParser &_parent)
			:parent(_parent) {}

	protected:
		/* virtual methods from class ConfigParser */
		void ParseLine(FileLineParser &line) override;
		void Finish() override;
	};

public:
	explicit BpConfigParser(BpConfig &_config)
		:config(_config) {}

protected:
	/* virtual methods from class NestedConfigParser */
	void ParseLine2(FileLineParser &line) override;
	void FinishChild(std::unique_ptr<ConfigParser> &&child) override;

private:
	void CreateListener(FileLineParser &line);
	void CreateControl(FileLineParser &line);
};

class SslClientConfigParser : public ConfigParser {
	SslClientConfig config;

public:
	SslClientConfig &&GetConfig() noexcept {
		return std::move(config);
	}

protected:
	/* virtual methods from class ConfigParser */
	void ParseLine(FileLineParser &line) override;
};

void
SslClientConfigParser::ParseLine(FileLineParser &line)
{
	const char *word = line.ExpectWord();

	if (StringIsEqual(word, "cert")) {
		const char *cert_file = line.ExpectValue();
		const char *key_file = line.ExpectValue();

		std::string name;
		if (!line.IsEnd())
			name = line.ExpectValue();

		line.ExpectEnd();

		config.cert_key.emplace_back(std::move(name), cert_file, key_file);
	} else
		throw LineParser::Error("Unknown option");
}

static BpListenerConfig::Handler
ParseListenerHandler(const char *s)
{
	if (StringIsEqual(s, "translation"))
		return BpListenerConfig::Handler::TRANSLATION;
	else if (StringIsEqual(s, "prometheus_exporter"))
		return BpListenerConfig::Handler::PROMETHEUS_EXPORTER;
	else
		throw LineParser::Error("Unknown handler");
}

void
BpConfigParser::Listener::ParseLine(FileLineParser &line)
{
	const char *word = line.ExpectWord();

	if (StringIsEqual(word, "bind")) {
		if (!config.bind_address.IsNull())
			throw LineParser::Error("Bind address already specified");

		config.bind_address = ParseSocketAddress(line.ExpectValueAndEnd(),
							 80, true);
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
	} else if (StringIsEqual(word, "zeroconf_service") ||
		   /* old option name: */ StringIsEqual(word, "zeroconf_type")) {
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
	} else if (StringIsEqual(word, "zeroconf_weight")) {
#ifdef HAVE_AVAHI
		if (config.zeroconf_service.empty())
			throw LineParser::Error("zeroconf_weight without zeroconf_service");

		if (config.zeroconf_weight >= 0)
			throw LineParser::Error("Duplicate zeroconf_weight");

		const char *s = line.ExpectValueAndEnd();

		char *endptr;
		config.zeroconf_weight = strtod(s, &endptr);
		if (endptr == s || *endptr != '\0')
			throw LineParser::Error("Failed to parse number");

		if (config.zeroconf_weight <= 0 || config.zeroconf_weight > 1e6f)
			throw LineParser::Error("Bad zeroconf_weight value");
#else
		throw LineParser::Error("Zeroconf support is disabled at compile time");
#endif
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
	} else if (StringIsEqual(word, "auth_alt_host")) {
		config.auth_alt_host = line.NextBool();
		line.ExpectEnd();
	} else if (StringIsEqual(word, "ssl")) {
		bool value = line.NextBool();

		if (config.ssl && !value)
			throw LineParser::Error("SSL cannot be disabled at this point");

		line.ExpectEnd();

		config.ssl = value;
	} else if (StringIsEqual(word, "ssl_cert")) {
		if (!config.ssl)
			throw LineParser::Error("SSL is not enabled");

		const char *path = line.ExpectValue();
		const char *key_path = line.ExpectValue();
		line.ExpectEnd();

		config.ssl_config.cert_key.emplace_back(path, key_path);
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
	} else if (StringIsEqual(word, "translation_socket")) {
		config.translation_sockets.emplace_front(line.ExpectValueAndEnd());
	} else if (StringIsEqual(word, "handler")) {
		config.handler = ParseListenerHandler(line.ExpectValueAndEnd());
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
	} else
		throw LineParser::Error("Unknown option");
}

void
BpConfigParser::Listener::Finish()
{
	if (config.bind_address.IsNull())
		throw LineParser::Error("Listener has no bind address");

	if (config.ssl && config.ssl_config.cert_key.empty())
		throw LineParser::Error("No SSL certificates ");

	if (!config.translation_sockets.empty() && config.handler != BpListenerConfig::Handler::TRANSLATION)
		throw LineParser::Error("Translation servers only possible for handler=translation");

	parent.config.listen.emplace_front(std::move(config));

	ConfigParser::Finish();
}

inline void
BpConfigParser::CreateListener(FileLineParser &line)
{
	line.ExpectSymbolAndEol('{');

	SetChild(std::make_unique<Listener>(*this));
}

void
BpConfigParser::Control::ParseLine(FileLineParser &line)
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
BpConfigParser::Control::Finish()
{
	if (config.bind_address.IsNull())
		throw LineParser::Error("Bind address is missing");

	config.Fixup();

	parent.config.control_listen.emplace_front(std::move(config));

	ConfigParser::Finish();
}

inline void
BpConfigParser::CreateControl(FileLineParser &line)
{
	line.ExpectSymbolAndEol('{');
	SetChild(std::make_unique<Control>(*this));
}

void
BpConfigParser::ParseLine2(FileLineParser &line)
{
	const char *word = line.ExpectWord();

	if (StringIsEqual(word, "listener"))
		CreateListener(line);
	else if (StringIsEqual(word, "control"))
		CreateControl(line);
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
	} else if (StringIsEqual(word, "child_error_logger")) {
		line.ExpectSymbolAndEol('{');

		current_access_log = &config.child_error_log;
		SetChild(std::make_unique<AccessLogConfigParser>(true));
	} else if (StringIsEqual(word, "set")) {
		const char *name = line.ExpectWord();
		line.ExpectSymbol('=');
		const char *value = line.ExpectValueAndEnd();
		config.HandleSet(name, value);
	} else if (StringIsEqual(word, "spawn")) {
		line.ExpectSymbolAndEol('{');
		SetChild(std::make_unique<SpawnConfigParser>(config.spawn));
	} else if (StringIsEqual(word, "ssl_client")) {
		line.ExpectSymbolAndEol('{');
		SetChild(std::make_unique<SslClientConfigParser>());
	} else if (StringIsEqual(word, "emulate_mod_auth_easy")) {
		config.emulate_mod_auth_easy = line.NextBool();
		line.ExpectEnd();
	} else if (StringIsEqual(word, "translation_socket")) {
		config.translation_sockets.emplace_front(line.ExpectValueAndEnd());
	} else
		throw LineParser::Error("Unknown option");
}

void
BpConfigParser::FinishChild(std::unique_ptr<ConfigParser> &&c)
{
	if (auto *al = dynamic_cast<AccessLogConfigParser *>(c.get())) {
		*current_access_log = al->GetConfig();
	} else if (auto *sc = dynamic_cast<SslClientConfigParser *>(c.get())) {
		config.ssl_client = sc->GetConfig();
	}
}

void
LoadConfigFile(BpConfig &config, const char *path)
{
	BpConfigParser parser(config);
	VariableConfigParser v_parser(parser);
	CommentConfigParser parser2(v_parser);
	IncludeConfigParser parser3(path, parser2);

	ParseConfigFile(path, parser3);
}
