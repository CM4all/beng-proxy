// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <max.kellermann@ionos.com>

#pragma once

#include "stock/AbstractStock.hxx"

class StockLoggerDomain {
	const AbstractStock &stock;

public:
	explicit StockLoggerDomain(const AbstractStock &_stock) noexcept
		:stock(_stock) {}

	std::string_view GetDomain() const noexcept {
		return stock.GetNameView();
	}
};
