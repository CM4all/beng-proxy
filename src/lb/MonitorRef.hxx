// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#pragma once

#include <utility>

class LbMonitorStock;
class LbMonitorController;

class LbMonitorRef final {
	LbMonitorStock &stock;
	LbMonitorController *controller;

public:
	LbMonitorRef(LbMonitorStock &_stock,
		     LbMonitorController &_controller) noexcept;

	LbMonitorRef(LbMonitorRef &&src) noexcept
		:stock(src.stock), controller(std::exchange(src.controller, nullptr)) {}

	~LbMonitorRef() noexcept {
		if (controller != nullptr)
			Release();
	}

private:
	void Release() noexcept;
};
