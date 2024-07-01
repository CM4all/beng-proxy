// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#pragma once

#include "spawn/ChildOptions.hxx"

class AllocatorPtr;
class MatchData;

/**
 * The description of a delegate process.
 */
struct DelegateAddress {
	const char *delegate;

	/**
	 * Options for the delegate process.
	 */
	ChildOptions child_options;

	DelegateAddress(const char *_delegate) noexcept;

	constexpr DelegateAddress(ShallowCopy shallow_copy,
				  const DelegateAddress &src) noexcept
		:delegate(src.delegate),
		 child_options(shallow_copy, src.child_options) {}

	constexpr DelegateAddress(DelegateAddress &&src) noexcept
		:DelegateAddress(ShallowCopy(), src) {}

	DelegateAddress(AllocatorPtr alloc, const DelegateAddress &src) noexcept;

	DelegateAddress &operator=(const DelegateAddress &) = delete;

	/**
	 * Throws std::runtime_error on error.
	 */
	void Check() const {
		child_options.Check();
	}

	/**
	 * Does this object need to be expanded with Expand()?
	 */
	[[gnu::pure]]
	bool IsExpandable() const noexcept {
		return child_options.IsExpandable();
	}

	/**
	 * Throws std::runtime_error on error.
	 */
	void Expand(AllocatorPtr alloc, const MatchData &match_data);
};
