// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <max.kellermann@ionos.com>

#pragma once

#include <string_view>

class GrowingBuffer;
struct SpawnStats;
struct ChildProcessTerminatorStats;

namespace Prometheus {

void
Write(GrowingBuffer &buffer, std::string_view process,
      const ChildProcessTerminatorStats &stats) noexcept;

void
Write(GrowingBuffer &buffer, std::string_view process,
      const SpawnStats &stats) noexcept;

} // namespace Prometheus
