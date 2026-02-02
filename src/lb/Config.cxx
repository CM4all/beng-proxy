// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <max.kellermann@ionos.com>

#include "Config.hxx"
#include "Check.hxx"
#include "util/StringParser.hxx"

#include <stdexcept>

using std::string_view_literals::operator""sv;

LbConfig::LbConfig() noexcept = default;
LbConfig::~LbConfig() noexcept = default;

void
LbConfig::HandleSet(std::string_view name, const char *value)
{
	if (name == "tcp_stock_limit") {
		tcp_stock_limit = ParseUnsignedLong(value);
	} else if (name == "populate_io_buffers"sv) {
		populate_io_buffers = ParseBool(value);
	} else
		throw std::runtime_error("Unknown variable");
}
