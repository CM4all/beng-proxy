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

	DelegateAddress(const char *_delegate);

	constexpr DelegateAddress(ShallowCopy shallow_copy,
				  const DelegateAddress &src)
		:delegate(src.delegate),
		 child_options(shallow_copy, src.child_options) {}

	constexpr DelegateAddress(DelegateAddress &&src)
		:DelegateAddress(ShallowCopy(), src) {}

	DelegateAddress(AllocatorPtr alloc, const DelegateAddress &src);

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
	bool IsExpandable() const {
		return child_options.IsExpandable();
	}

	/**
	 * Throws std::runtime_error on error.
	 */
	void Expand(AllocatorPtr alloc, const MatchData &match_data);
};
