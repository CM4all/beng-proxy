// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#pragma once

#include <exception>
#include <utility>

struct pool;
class UnusedIstreamPtr;

class InjectIstreamControl {
protected:
	InjectIstreamControl() = default;
	~InjectIstreamControl() = default;

	InjectIstreamControl(const InjectIstreamControl &) = delete;
	InjectIstreamControl &operator=(const InjectIstreamControl &) = delete;

public:
	/**
	 * Injects a failure.
	 */
	void InjectFault(std::exception_ptr e) noexcept;
};

/**
 * Fault injection istream filter.  This istream forwards data from
 * its input, but will never forward eof/abort.  The "abort" can be
 * injected at any time.
 */
std::pair<UnusedIstreamPtr, InjectIstreamControl &>
istream_inject_new(struct pool &pool, UnusedIstreamPtr input) noexcept;
