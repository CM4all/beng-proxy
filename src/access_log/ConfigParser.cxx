// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#include "ConfigParser.hxx"
#include "net/Parser.hxx"
#include "net/log/Protocol.hxx"
#include "io/config/FileLineParser.hxx"

#include <cstring>

void
AccessLogConfigParser::ParseLine(FileLineParser &line)
{
	const char *word = line.ExpectWord();

	if (strcmp(word, "enabled") == 0 && !is_child_error_logger) {
		enabled = line.NextBool();
		line.ExpectEnd();
	} else if (strcmp(word, "send_to") == 0) {
		if (type_selected)
			throw LineParser::Error("Access logger already defined");

		type_selected = true;
		config.type = AccessLogConfig::Type::SEND;
		config.send_to = ParseSocketAddress(line.ExpectValueAndEnd(),
						    Net::Log::DEFAULT_PORT, false);
	} else if (strcmp(word, "shell") == 0) {
		if (type_selected)
			throw LineParser::Error("Access logger already defined");

		type_selected = true;
		config.type = AccessLogConfig::Type::EXECUTE;
		config.command = line.ExpectValueAndEnd();
	} else if (strcmp(word, "ignore_localhost_200") == 0 &&
		   !is_child_error_logger) {
		config.ignore_localhost_200 = line.ExpectValueAndEnd();
	} else if (strcmp(word, "trust_xff") == 0 && !is_child_error_logger) {
		const char *value = line.ExpectValueAndEnd();

		if (*value != '/' && *value != '@' && strchr(value, '/') != nullptr)
			config.xff.trust_networks.emplace_front(value);
		else
			config.xff.trust.emplace(value);
	} else if (strcmp(word, "trust_xff_interface") == 0 && !is_child_error_logger) {
		config.xff.trust_interfaces.emplace(line.ExpectValueAndEnd());
	} else if (strcmp(word, "forward_child_errors") == 0 &&
		   !is_child_error_logger) {
		config.forward_child_errors = line.NextBool();
		line.ExpectEnd();
	} else if (strcmp(word, is_child_error_logger ? "rate_limit" : "child_error_rate_limit") == 0) {
		if (!is_child_error_logger && !config.forward_child_errors)
			throw LineParser::Error("Requires forward_child_errors");

		config.child_error_options.rate_limit = line.NextPositiveInteger();
		config.child_error_options.burst = line.NextPositiveInteger();

		if (config.child_error_options.burst < config.child_error_options.rate_limit)
			throw LineParser::Error("Burst must not be smaller than the rate");

		line.ExpectEnd();
	} else if (strcmp(word, is_child_error_logger ? "is_default" : "child_error_is_default") == 0) {
		if (!is_child_error_logger && !config.forward_child_errors)
			throw LineParser::Error("Requires forward_child_errors");

		config.child_error_options.is_default = line.NextBool();
		line.ExpectEnd();
	} else
		throw LineParser::Error("Unknown option");
}

void
AccessLogConfigParser::Finish()
{
	if (is_child_error_logger)
		config.forward_child_errors = true;

	if (!enabled) {
		config.type = AccessLogConfig::Type::DISABLED;
		type_selected = true;
	}

	if (!type_selected)
		throw std::runtime_error("Empty access_logger block");
}
