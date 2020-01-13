/*
 * Copyright 2007-2020 CM4all GmbH
 * All rights reserved.
 *
 * author: Max Kellermann <mk@cm4all.com>
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * - Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 *
 * - Redistributions in binary form must reproduce the above copyright
 * notice, this list of conditions and the following disclaimer in the
 * documentation and/or other materials provided with the
 * distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE
 * FOUNDATION OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED
 * OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#pragma once

#include "FailureStatus.hxx"
#include "util/Compiler.h"

#include <boost/intrusive/unordered_set.hpp>

class Expiry;
class SocketAddress;
class FailureInfo;
class ReferencedFailureInfo;

/*
 * Remember which servers (socket addresses) failed recently.
 */
class FailureManager {
	class Failure;

	struct Hash {
		gcc_pure
		size_t operator()(const SocketAddress a) const noexcept;

		gcc_pure
		size_t operator()(const Failure &f) const noexcept;
	};

	struct Equal {
		gcc_pure
		bool operator()(const SocketAddress a,
				const SocketAddress b) const noexcept;

		gcc_pure
		bool operator()(const SocketAddress a,
				const Failure &b) const noexcept;
	};

	typedef boost::intrusive::unordered_set<Failure,
						boost::intrusive::base_hook<boost::intrusive::unordered_set_base_hook<boost::intrusive::link_mode<boost::intrusive::normal_link>>>,
						boost::intrusive::hash<Hash>,
						boost::intrusive::equal<Equal>,
						boost::intrusive::constant_time_size<false>> FailureSet;

	static constexpr size_t N_BUCKETS = 97;

	FailureSet::bucket_type buckets[N_BUCKETS];

	FailureSet failures;

public:
	FailureManager() noexcept
		:failures(FailureSet::bucket_traits(buckets, N_BUCKETS)) {}

	~FailureManager() noexcept;

	FailureManager(const FailureManager &) = delete;
	FailureManager &operator=(const FailureManager &) = delete;

	/**
	 * Looks up a #FailureInfo instance or creates a new one.  The
	 * return value should be passed to the #FailureRef constructor.
	 */
	ReferencedFailureInfo &Make(SocketAddress address) noexcept;

	SocketAddress GetAddress(const FailureInfo &info) const noexcept;

	gcc_pure
	FailureStatus Get(Expiry now, SocketAddress address) const noexcept;

	gcc_pure
	bool Check(Expiry now, SocketAddress address,
		   bool allow_fade=false) const noexcept;
};
