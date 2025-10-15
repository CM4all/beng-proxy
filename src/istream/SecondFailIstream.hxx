// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <max.kellermann@ionos.com>

#pragma once

#include <exception>

struct pool;
class UnusedIstreamPtr;

/**
 * An #Istream filter which fails on the second attempt to obtain
 * data.
 */
UnusedIstreamPtr
NewSecondFailIstream(struct pool &pool, UnusedIstreamPtr &&input,
		     std::exception_ptr &&error) noexcept;
