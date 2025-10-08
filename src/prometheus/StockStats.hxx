// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <max.kellermann@ionos.com>

#pragma once

#include <string_view>

class GrowingBuffer;
struct StockStats;

namespace Prometheus {

void
Write(GrowingBuffer &buffer, std::string_view process,
      std::string_view stock,
      const StockStats &stats) noexcept;

} // namespace Prometheus
