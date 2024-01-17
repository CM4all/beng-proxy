// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#pragma once

#include <string_view>

class GrowingBuffer;
namespace BengControl { struct Stats; }

namespace Prometheus {

void
Write(GrowingBuffer &buffer, std::string_view process,
      const BengControl::Stats &stats) noexcept;

} // namespace Prometheus
