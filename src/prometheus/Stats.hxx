// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#pragma once

#include <string_view>

class GrowingBuffer;
namespace BengProxy { struct ControlStats; }

namespace Prometheus {

void
Write(GrowingBuffer &buffer, std::string_view process,
      const BengProxy::ControlStats &stats) noexcept;

} // namespace Prometheus
