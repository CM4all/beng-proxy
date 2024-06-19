// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#pragma once

#include "http/StatusIndex.hxx"

#include <array>
#include <cstdint>

using PerHttpStatusCounters = std::array<uint_least64_t, valid_http_status_array.size()>;
