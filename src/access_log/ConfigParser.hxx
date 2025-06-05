// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <max.kellermann@ionos.com>

#pragma once

#include "Config.hxx"
#include "io/config/ConfigParser.hxx"

/**
 * Configuration which describes whether and how to log HTTP requests.
 */
class AccessLogConfigParser : public ConfigParser {
	AccessLogConfig config;
	bool enabled = true, type_selected = false;

	const bool is_child_error_logger;

public:
	explicit AccessLogConfigParser(bool _is_child_error_logger=false) noexcept
		:is_child_error_logger(_is_child_error_logger) {}

	bool IsChildErrorLogger() const noexcept {
		return is_child_error_logger;
	}

	AccessLogConfig &&GetConfig() {
		return std::move(config);
	}

protected:
	/* virtual methods from class ConfigParser */
	void ParseLine(FileLineParser &line) override;
	void Finish() override;
};
