// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#pragma once

#include <string_view>

class GrowingBuffer;
struct HttpStats;
struct TaggedHttpStats;

namespace Prometheus {

void
Write(GrowingBuffer &buffer, std::string_view process,
      std::string_view listener,
      const HttpStats &stats) noexcept;

void
Write(GrowingBuffer &buffer, std::string_view process,
      std::string_view listener,
      const TaggedHttpStats &stats) noexcept;

} // namespace Prometheus
