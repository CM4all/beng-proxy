// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#pragma once

#include <cstddef>
#include <span>
#include <string_view>

class AllocatorPtr;
class MatchData;

/**
 * The address of a file on a NFS server.
 */
struct NfsAddress {
	const char *server;

	const char *export_name;

	const char *path;

	const char *content_type;

	std::span<const std::byte> content_type_lookup{};

	/**
	 * The value of #TRANSLATE_EXPAND_PATH.  Only used by the
	 * translation cache.
	 */
	bool expand_path = false;

	NfsAddress(const char *_server,
		   const char *_export_name, const char *_path)
		:server(_server), export_name(_export_name), path(_path),
		 content_type(nullptr) {}

	NfsAddress(AllocatorPtr alloc, const NfsAddress &other);

	NfsAddress(const NfsAddress &) = delete;
	NfsAddress &operator=(const NfsAddress &) = delete;

	const char *GetId(AllocatorPtr alloc) const;

	/**
	 * Throws std::runtime_error on error.
	 */
	void Check() const;

	[[gnu::pure]]
	bool HasQueryString() const {
		return false;
	}

	[[gnu::pure]]
	bool IsValidBase() const;

	NfsAddress *SaveBase(AllocatorPtr alloc,
			     std::string_view suffix) const noexcept;

	NfsAddress *LoadBase(AllocatorPtr alloc,
			     std::string_view suffix) const noexcept;

	/**
	 * Does this address need to be expanded with Expand()?
	 */
	[[gnu::pure]]
	bool IsExpandable() const {
		return expand_path;
	}

	/**
	 * Throws std::runtime_error on error.
	 */
	const NfsAddress *Expand(AllocatorPtr alloc,
				 const MatchData &match_data) const;
};
