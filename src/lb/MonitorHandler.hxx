// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <max.kellermann@ionos.com>

#pragma once

#include <exception>

class LbMonitorHandler {
public:
	virtual void Success() noexcept = 0;
	virtual void Fade() noexcept = 0;
	virtual void Timeout() noexcept = 0;
	virtual void Error(std::exception_ptr e) noexcept = 0;
};
