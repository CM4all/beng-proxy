/*
 * Copyright 2007-2021 CM4all GmbH
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

#include "util/ConstBuffer.hxx"

class AllocatorPtr;
class MatchInfo;
struct DelegateAddress;

/**
 * The address of a local static file.
 */
struct FileAddress {
	const char *path;
	const char *deflated = nullptr;
	const char *gzipped = nullptr;

	/**
	 * Absolute path of a directory below which the other paths
	 * (#path, #deflated, #gzipped) are located.
	 */
	const char *base = nullptr;

	const char *content_type = nullptr;

	ConstBuffer<void> content_type_lookup = nullptr;

	const char *document_root = nullptr;

	DelegateAddress *delegate = nullptr;

	bool auto_gzipped = false;

	/**
	 * The value of #TRANSLATE_EXPAND_PATH.  Only used by the
	 * translation cache.
	 */
	bool expand_path = false;

	/**
	 * The value of #TRANSLATE_EXPAND_DOCUMENT_ROOT.  Only used by the
	 * translation cache.
	 */
	bool expand_document_root = false;

	/**
	 * @param _path the new path pointer (taken as-is, no deep copy)
	 */
	explicit constexpr FileAddress(const char *_path) noexcept
		:path(_path)
	{
	}

	/**
	 * Copy from an existing #FileAddress instance, but override the
	 * path.
	 *
	 * @param _path the new path pointer (taken as-is, no deep copy)
	 */
	FileAddress(AllocatorPtr alloc, const FileAddress &src,
		    const char *_path) noexcept;

	FileAddress(AllocatorPtr alloc, const FileAddress &src) noexcept;

	FileAddress(const FileAddress &) = delete;
	FileAddress &operator=(const FileAddress &) = delete;

	[[gnu::pure]]
	bool HasQueryString() const noexcept {
		return false;
	}

	/**
	 * Throws std::runtime_error on error.
	 */
	void Check() const;

	[[gnu::pure]]
	bool IsValidBase() const noexcept;

	bool SplitBase(AllocatorPtr alloc, const char *suffix) noexcept;

	FileAddress *SaveBase(AllocatorPtr alloc,
			      const char *suffix) const noexcept;
	FileAddress *LoadBase(AllocatorPtr alloc,
			      const char *suffix) const noexcept;

	/**
	 * Does this address need to be expanded with Expand()?
	 */
	[[gnu::pure]]
	bool IsExpandable() const noexcept;

	/**
	 * Throws std::runtime_error on error.
	 */
	void Expand(AllocatorPtr alloc, const MatchInfo &match_info);
};
