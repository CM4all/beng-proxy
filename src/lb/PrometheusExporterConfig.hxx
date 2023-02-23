// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#pragma once

#include "net/AllocatedSocketAddress.hxx"

#include <string>
#include <forward_list>

struct LbPrometheusExporterConfig {
	std::string name;

	std::forward_list<AllocatedSocketAddress> load_from_local;

	explicit LbPrometheusExporterConfig(const char *_name) noexcept
		:name(_name) {}
};
