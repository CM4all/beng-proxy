// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#include "Config.hxx"
#include "lib/fmt/RuntimeError.hxx"
#include "io/config/ConfigParser.hxx"
#include "io/config/FileLineParser.hxx"
#include "util/HexParse.hxx"
#include "util/StringAPI.hxx"

#include <stdexcept>

const WrapKey &
CertDatabaseConfig::GetWrapKey(std::string_view name) const
{
	const auto i = wrap_keys.find(name);
	if (i == wrap_keys.end())
		throw FmtRuntimeError("No such wrap_key: {}", name);

	return i->second;
}

std::pair<const char *, const WrapKey *>
CertDatabaseConfig::GetDefaultWrapKey() const
{
	if (default_wrap_key.empty())
		return {};

	return {
		default_wrap_key.c_str(),
		&GetWrapKey(default_wrap_key),
	};
}

bool
CertDatabaseConfig::ParseLine(const char *word, LineParser &line)
{
	if (StringIsEqual(word, "connect")) {
		connect = line.ExpectValueAndEnd();
		return true;
	} else if (StringIsEqual(word, "schema")) {
		schema = line.ExpectValueAndEnd();
		return true;
	} else if (StringIsEqual(word, "wrap_key")) {
		const char *name = line.ExpectValue();
		const char *hex_key = line.ExpectValue();
		line.ExpectEnd();

		WrapKeyBuffer key;
		if (!ParseLowerHexFixed(hex_key, key))
			throw LineParser::Error("Malformed AES256 key");

		auto i = wrap_keys.emplace(name, key);
		if (!i.second)
			throw LineParser::Error("Duplicate wrap_key name");

		if (default_wrap_key.empty())
			default_wrap_key = i.first->first;

		return true;
	} else
		return false;
}

void
CertDatabaseConfig::Check()
{
	if (connect.empty())
		throw std::runtime_error("Missing 'connect'");
}

CertDatabaseConfig
LoadStandaloneCertDatabaseConfig(const char *path)
{
	struct StandaloneCertDatabaseConfigParser final : ConfigParser {
		CertDatabaseConfig config;

		/* virtual methods from class ConfigParser */
		void ParseLine(FileLineParser &line) override {
			const char *word = line.ExpectWord();
			if (!config.ParseLine(word, line))
				throw std::runtime_error{"Unknown option"};
		}

		void Finish() override {
			config.Check();
		}
	} parser;

	VariableConfigParser v_parser(parser);
	CommentConfigParser parser2(v_parser);
	IncludeConfigParser parser3(path, parser2);

	ParseConfigFile(path, parser3);

	return std::move(parser.config);
}
