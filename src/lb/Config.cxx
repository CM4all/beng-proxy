// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <max.kellermann@ionos.com>

#include "Config.hxx"
#include "Check.hxx"
#include "util/StringParser.hxx"

#include <stdexcept>

LbConfig::LbConfig() noexcept = default;
LbConfig::~LbConfig() noexcept = default;

void
LbConfig::HandleSet(std::string_view name, const char *value)
{
	if (name == "tcp_stock_limit") {
		tcp_stock_limit = ParseUnsignedLong(value);
	} else
		throw std::runtime_error("Unknown variable");
}
