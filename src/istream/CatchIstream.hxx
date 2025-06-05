// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <max.kellermann@ionos.com>

#pragma once

#include "util/BindMethod.hxx"

#include <exception>

struct pool;
class UnusedIstreamPtr;

/**
 * This istream filter catches fatal errors and attempts to ignore
 * them.
 */
UnusedIstreamPtr
NewCatchIstream(struct pool *pool, UnusedIstreamPtr input,
		BoundMethod<std::exception_ptr(std::exception_ptr ep) noexcept> callback) noexcept;
