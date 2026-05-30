// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <max.kellermann@ionos.com>

#pragma once

#include <string_view>

class GrowingBuffer;
struct CgroupPressureStats;

namespace Prometheus {

void
Write(GrowingBuffer &buffer, std::string_view resource,
      const CgroupPressureStats &stats) noexcept;

} // namespace Prometheus
