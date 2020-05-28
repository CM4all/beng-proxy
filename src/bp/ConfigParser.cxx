/*
 * Copyright 2007-2020 CM4all GmbH
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

#include "Config.hxx"
#include "access_log/ConfigParser.hxx"
#include "spawn/ConfigParser.hxx"
#include "io/FileLineParser.hxx"
#include "io/ConfigParser.hxx"
#include "net/Parser.hxx"
#include "util/StringView.hxx"
#include "util/StringParser.hxx"

#ifdef HAVE_AVAHI
#include "avahi/Check.hxx"
#endif

#include <string.h>

class BpConfigParser final : public NestedConfigParser {
	BpConfig &config;

	class Listener final : public ConfigParser {
		BpConfigParser &parent;
		BpConfig::Listener config;

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

	if (strcmp(word, "cert") == 0) {
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

void
BpConfigParser::Listener::ParseLine(FileLineParser &line)
{
	const char *word = line.ExpectWord();

	if (strcmp(word, "bind") == 0) {
		if (!config.bind_address.IsNull())
			throw LineParser::Error("Bind address already specified");

		config.bind_address = ParseSocketAddress(line.ExpectValueAndEnd(),
							 80, true);
	} else if (strcmp(word, "interface") == 0) {
		config.interface = line.ExpectValueAndEnd();
	} else if (strcmp(word, "tag") == 0) {
		config.tag = line.ExpectValueAndEnd();
	} else if (strcmp(word, "zeroconf_service") == 0 ||
		   /* old option name: */ strcmp(word, "zeroconf_type") == 0) {
#ifdef HAVE_AVAHI
		config.zeroconf_service = MakeZeroconfServiceType(line.ExpectValueAndEnd(),
								  "_tcp");
#else
		throw LineParser::Error("Zeroconf support is disabled at compile time");
#endif
	} else if (strcmp(word, "ack_timeout") == 0) {
		config.tcp_user_timeout = line.NextPositiveInteger() * 1000;
		line.ExpectEnd();
	} else if (strcmp(word, "keepalive") == 0) {
		config.keepalive = line.NextBool();
		line.ExpectEnd();
	} else if (strcmp(word, "v6only") == 0) {
		config.v6only = line.NextBool();
		line.ExpectEnd();
	} else if (strcmp(word, "reuse_port") == 0) {
		config.reuse_port = line.NextBool();
		line.ExpectEnd();
	} else if (strcmp(word, "free_bind") == 0) {
		config.free_bind = line.NextBool();
		line.ExpectEnd();
	} else if (strcmp(word, "auth_alt_host") == 0) {
		config.auth_alt_host = line.NextBool();
		line.ExpectEnd();
	} else if (strcmp(word, "ssl") == 0) {
		bool value = line.NextBool();

		if (config.ssl && !value)
			throw LineParser::Error("SSL cannot be disabled at this point");

		line.ExpectEnd();

		config.ssl = value;
	} else if (strcmp(word, "ssl_cert") == 0) {
		if (!config.ssl)
			throw LineParser::Error("SSL is not enabled");

		const char *path = line.ExpectValue();
		const char *key_path = line.ExpectValue();
		line.ExpectEnd();

		config.ssl_config.cert_key.emplace_back(path, key_path);
	} else if (strcmp(word, "ssl_ca_cert") == 0) {
		if (!config.ssl)
			throw LineParser::Error("SSL is not enabled");

		if (!config.ssl_config.ca_cert_file.empty())
			throw LineParser::Error("Certificate already configured");

		config.ssl_config.ca_cert_file = line.ExpectValueAndEnd();
	} else if (strcmp(word, "ssl_verify") == 0) {
		if (!config.ssl)
			throw LineParser::Error("SSL is not enabled");

		const char *value = line.ExpectValueAndEnd();
		if (strcmp(value, "yes") == 0)
			config.ssl_config.verify = SslVerify::YES;
		else if (strcmp(value, "no") == 0)
			config.ssl_config.verify = SslVerify::NO;
		else if (strcmp(value, "optional") == 0)
			config.ssl_config.verify = SslVerify::OPTIONAL;
		else
			throw LineParser::Error("yes/no expected");
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

	if (strcmp(word, "bind") == 0) {
		config.bind_address = ParseSocketAddress(line.ExpectValueAndEnd(),
							 5478, true);
	} else if (strcmp(word, "multicast_group") == 0) {
		config.multicast_group = ParseSocketAddress(line.ExpectValueAndEnd(),
							    0, false);
	} else if (strcmp(word, "interface") == 0) {
		config.interface = line.ExpectValueAndEnd();
	} else
		throw LineParser::Error("Unknown option");
}

void
BpConfigParser::Control::Finish()
{
	if (config.bind_address.IsNull())
		throw LineParser::Error("Bind address is missing");

	if (config.multicast_group.IsNull() &&
	    !parent.config.multicast_group.IsNull())
		/* default to the --multicast-group setting (for backwards
		   compatibility) */
		config.multicast_group = parent.config.multicast_group;

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

	if (strcmp(word, "listener") == 0)
		CreateListener(line);
	else if (strcmp(word, "control") == 0)
		CreateControl(line);
	else if (strcmp(word, "access_logger") == 0) {
		if (line.SkipSymbol('{')) {
			line.ExpectEnd();

			SetChild(std::make_unique<AccessLogConfigParser>());
		} else
			/* <12.0.32 legacy */
			config.access_log.SetLegacy(line.ExpectValueAndEnd());
	} else if (strcmp(word, "child_error_logger") == 0) {
		line.ExpectSymbolAndEol('{');
		SetChild(std::make_unique<AccessLogConfigParser>(true));
	} else if (strcmp(word, "set") == 0) {
		const char *name = line.ExpectWord();
		line.ExpectSymbol('=');
		const char *value = line.ExpectValueAndEnd();
		config.HandleSet(name, value);
	} else if (strcmp(word, "spawn") == 0) {
		line.ExpectSymbolAndEol('{');
		SetChild(std::make_unique<SpawnConfigParser>(config.spawn));
	} else if (strcmp(word, "ssl_client") == 0) {
		line.ExpectSymbolAndEol('{');
		SetChild(std::make_unique<SslClientConfigParser>());
	} else if (StringIsEqual(word, "emulate_mod_auth_easy")) {
		config.emulate_mod_auth_easy = line.NextBool();
		line.ExpectEnd();
	} else
		throw LineParser::Error("Unknown option");
}

void
BpConfigParser::FinishChild(std::unique_ptr<ConfigParser> &&c)
{
	if (auto *al = dynamic_cast<AccessLogConfigParser *>(c.get())) {
		if (al->IsChildErrorLogger())
			config.child_error_log = al->GetConfig();
		else
			config.access_log = al->GetConfig();
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
