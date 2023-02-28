// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#pragma once

#include <string_view>

class WasMetricsHandler {
public:
	virtual void OnWasMetric(std::string_view name, float value) noexcept = 0;
};
