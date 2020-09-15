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

#include "ResourceAddress.hxx"
#include "util/Compiler.h"

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
	gcc_pure
	bool IsExpandable() const noexcept {
		return address.IsExpandable();
	}

	gcc_pure
	const char *GetId(AllocatorPtr alloc) const noexcept;

	/**
	 * Expand the strings in this transformation (not following the linked
	 * lits) with the specified regex result.
	 *
	 * Throws std::runtime_error on error.
	 */
	void Expand(AllocatorPtr alloc, const MatchInfo &match_info);
};
