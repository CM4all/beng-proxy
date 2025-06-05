// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <max.kellermann@ionos.com>

#pragma once

#include "ZeroconfDiscoveryConfig.hxx"

#include <string>

struct LbPrometheusDiscoveryConfig {
	std::string name;

	ZeroconfDiscoveryConfig zeroconf;

	explicit LbPrometheusDiscoveryConfig(const char *_name) noexcept
		:name(_name) {}
};
