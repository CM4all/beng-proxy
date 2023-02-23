// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#pragma once

class GrowingBuffer;
namespace BengProxy { struct ControlStats; }

namespace Prometheus {

void
Write(GrowingBuffer &buffer, const char *process,
      const BengProxy::ControlStats &stats) noexcept;

} // namespace Prometheus
