// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#pragma once

#include "ResourceAddress.hxx"

class AllocatorPtr;

struct FilterTransformation {
	/**
	 * @see TranslationCommand::CACHE_TAG
	 */
	const char *cache_tag = nullptr;

	ResourceAddress address = nullptr;

	/**
	 * Send the X-CM4all-BENG-User header to the filter?
	 */
	bool reveal_user = false;

	/**
	 * Don't send a request body to the filter?
	 */
	bool no_body = false;

	FilterTransformation() = default;

	FilterTransformation(AllocatorPtr alloc,
			     const FilterTransformation &src) noexcept;

	/**
	 * Does this transformation need to be expanded with
	 * transformation_expand()?
	 */
	[[gnu::pure]]
	bool IsExpandable() const noexcept {
		return address.IsExpandable();
	}

	[[gnu::pure]]
	StringWithHash GetId(AllocatorPtr alloc) const noexcept;

	/**
	 * Expand the strings in this transformation (not following the linked
	 * lits) with the specified regex result.
	 *
	 * Throws std::runtime_error on error.
	 */
	void Expand(AllocatorPtr alloc, const MatchData &match_data);
};
