// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#pragma once

class GrowingBuffer;
struct HttpStats;
struct TaggedHttpStats;

namespace Prometheus {

void
Write(GrowingBuffer &buffer, const char *process, const char *listener,
      const HttpStats &stats) noexcept;

void
Write(GrowingBuffer &buffer, const char *process, const char *listener,
      const TaggedHttpStats &stats) noexcept;

} // namespace Prometheus
