// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#pragma once

#include "pool/SharedPtr.hxx"

struct pool;
class Istream;
class UnusedIstreamPtr;
class OptionalIstream;

class OptionalIstreamControl {
	friend class OptionalIstream;

	OptionalIstream *optional;

public:
	explicit constexpr OptionalIstreamControl(OptionalIstream &_optional) noexcept
		:optional(&_optional) {}

	/**
	 * Allows the #Istream to resume, but does not trigger reading.
	 */
	void Resume() noexcept;

	/**
	 * Discard the stream contents.
	 */
	void Discard() noexcept;
};

/**
 * An istream facade which holds an optional istream.  It blocks until
 * it is told to resume or to discard the inner istream.  Errors are
 * reported to the handler immediately.
 */
std::pair<UnusedIstreamPtr, SharedPoolPtr<OptionalIstreamControl>>
istream_optional_new(struct pool &pool, UnusedIstreamPtr input) noexcept;
