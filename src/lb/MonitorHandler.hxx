// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <max.kellermann@ionos.com>

#pragma once

#include <exception>

class LbMonitorHandler {
public:
	virtual void Success() = 0;
	virtual void Fade() = 0;
	virtual void Timeout() = 0;
	virtual void Error(std::exception_ptr e) = 0;
};
