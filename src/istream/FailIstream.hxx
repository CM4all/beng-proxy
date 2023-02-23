// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#pragma once

#include <exception>

struct pool;
class UnusedIstreamPtr;

/**
 * istream implementation which produces a failure.
 */
UnusedIstreamPtr
istream_fail_new(struct pool &pool, std::exception_ptr ep) noexcept;
