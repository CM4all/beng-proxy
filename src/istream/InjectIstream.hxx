// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#pragma once

#include <exception>
#include <memory>
#include <utility>

struct pool;
class UnusedIstreamPtr;
class InjectIstream;

class InjectIstreamControl {
	friend class InjectIstream;
	friend bool InjectFault(std::shared_ptr<InjectIstreamControl> &&control, std::exception_ptr &&e) noexcept;

	InjectIstream *inject;

public:
	explicit constexpr InjectIstreamControl(InjectIstream &_inject) noexcept
		:inject(&_inject) {}

	InjectIstreamControl(const InjectIstreamControl &) = delete;
	InjectIstreamControl &operator=(const InjectIstreamControl &) = delete;
};

/**
 * Fault injection istream filter.  This istream forwards data from
 * its input, but will never forward eof/abort.  The "abort" can be
 * injected at any time.
 */
std::pair<UnusedIstreamPtr, std::shared_ptr<InjectIstreamControl>>
istream_inject_new(struct pool &pool, UnusedIstreamPtr input) noexcept;

/**
 * Injects a failure.
 *
 * @return true on success, false if the #InjectIstream has already
 * been closed
 */
bool
InjectFault(std::shared_ptr<InjectIstreamControl> &&control, std::exception_ptr &&e) noexcept;
