// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#pragma once

#include "FailureStatus.hxx"
#include "net/SocketAddress.hxx"
#include "util/IntrusiveHashSet.hxx"

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
		[[gnu::pure]]
		size_t operator()(const SocketAddress a) const noexcept;
	};

	struct GetKey {
		[[gnu::pure]]
		SocketAddress operator()(const Failure &f) const noexcept;
	};

	static constexpr size_t N_BUCKETS = 4096;

	using FailureSet =
		IntrusiveHashSet<Failure, N_BUCKETS,
				 IntrusiveHashSetOperators<Failure, GetKey, Hash,
							   std::equal_to<SocketAddress>>>;

	FailureSet failures;

public:
	FailureManager() noexcept;
	~FailureManager() noexcept;

	FailureManager(const FailureManager &) = delete;
	FailureManager &operator=(const FailureManager &) = delete;

	/**
	 * Looks up a #FailureInfo instance or creates a new one.  The
	 * return value should be passed to the #FailureRef constructor.
	 */
	[[gnu::pure]]
	ReferencedFailureInfo &Make(SocketAddress address) noexcept;

	[[gnu::pure]]
	static SocketAddress GetAddress(const FailureInfo &info) noexcept;

	[[gnu::pure]]
	FailureStatus Get(Expiry now, SocketAddress address) const noexcept;

	[[gnu::pure]]
	bool Check(Expiry now, SocketAddress address,
		   bool allow_fade=false) const noexcept;
};
