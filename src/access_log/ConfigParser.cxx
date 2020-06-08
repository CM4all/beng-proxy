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

#include "ConfigParser.hxx"
#include "net/Parser.hxx"
#include "io/FileLineParser.hxx"

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
						    5479, false);
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
		config.trust_xff.emplace(line.ExpectValueAndEnd());
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
