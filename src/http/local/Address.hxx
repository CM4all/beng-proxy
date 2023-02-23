// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#pragma once

#include "spawn/ChildOptions.hxx"
#include "adata/ExpandableStringList.hxx"
#include "util/ShallowCopy.hxx"

#include <string_view>

class AllocatorPtr;

/**
 * The address of a HTTP server that is launched and managed by
 * beng-proxy.
 */
struct LhttpAddress {
	const char *path;

	ExpandableStringList args;

	ChildOptions options;

	/**
	 * The host part of the URI (including the port, if any).
	 */
	const char *host_and_port;

	const char *uri;

	/**
	 * The maximum number of parallel child processes of this
	 * kind.
	 */
	unsigned parallelism = 0;

	/**
	 * The maximum number of concurrent connections to one instance.
	 */
	unsigned concurrency = 1;

	/**
	 * Pass a blocking listener socket to the child process?  The
	 * default is true; sets SOCK_NONBLOCK if false.
	 */
	bool blocking = true;

	/**
	 * The value of #TRANSLATE_EXPAND_PATH.  Only used by the
	 * translation cache.
	 */
	bool expand_uri = false;

	explicit LhttpAddress(const char *path) noexcept;

	constexpr LhttpAddress(ShallowCopy shallow_copy,
			       const LhttpAddress &src) noexcept
		:path(src.path),
		 args(shallow_copy, src.args),
		 options(shallow_copy, src.options),
		 host_and_port(src.host_and_port),
		 uri(src.uri),
		 parallelism(src.parallelism),
		 concurrency(src.concurrency),
		 blocking(src.blocking),
		 expand_uri(src.expand_uri)
	{
	}

	constexpr LhttpAddress(LhttpAddress &&src) noexcept
		:LhttpAddress(ShallowCopy(), src) {}

	LhttpAddress(ShallowCopy shallow_copy, const LhttpAddress &src,
		     const char *_uri) noexcept
		:LhttpAddress(shallow_copy, src)
	{
		uri = _uri;
	}

	LhttpAddress(AllocatorPtr alloc, const LhttpAddress &src) noexcept;

	LhttpAddress &operator=(const LhttpAddress &) = delete;

	/**
	 * Generates a string identifying the server process.  This can be
	 * used as a key in a hash table.  The string will be allocated by
	 * the specified pool.
	 */
	[[gnu::pure]]
	const char *GetServerId(AllocatorPtr alloc) const noexcept;

	/**
	 * Generates a string identifying the address.  This can be used as a
	 * key in a hash table.  The string will be allocated by the specified
	 * pool.
	 */
	[[gnu::pure]]
	const char *GetId(AllocatorPtr alloc) const noexcept;

	/**
	 * Throws std::runtime_error on error.
	 */
	void Check() const;

	[[gnu::pure]]
	bool IsSameProgram(const LhttpAddress &other) const noexcept;

	[[gnu::pure]]
	bool HasQueryString() const noexcept;

	LhttpAddress *Dup(AllocatorPtr alloc) const noexcept;

	LhttpAddress *DupWithUri(AllocatorPtr alloc,
				 const char *uri) const noexcept;

	/**
	 * Duplicates this #lhttp_address object and inserts the specified
	 * query string into the URI.
	 */
	[[gnu::malloc]]
	LhttpAddress *InsertQueryString(AllocatorPtr alloc,
					const char *query_string) const noexcept;

	/**
	 * Duplicates this #lhttp_address object and inserts the specified
	 * arguments into the URI.
	 */
	[[gnu::malloc]]
	LhttpAddress *InsertArgs(AllocatorPtr alloc,
				 std::string_view new_args,
				 std::string_view path_info) const noexcept;

	[[gnu::pure]]
	bool IsValidBase() const noexcept;

	LhttpAddress *SaveBase(AllocatorPtr alloc,
			       std::string_view suffix) const noexcept;

	LhttpAddress *LoadBase(AllocatorPtr alloc,
			       std::string_view suffix) const noexcept;

	/**
	 * @return a new object on success, src if no change is needed, nullptr
	 * on error
	 */
	const LhttpAddress *Apply(AllocatorPtr alloc,
				  std::string_view relative) const noexcept;

	[[gnu::pure]]
	std::string_view RelativeTo(const LhttpAddress &base) const noexcept;

	/**
	 * A combination of Apply() and RelativeTo(), i.e. calls
	 * apply_base.Apply(relative).RelativeTo(*this). It is cheaper
	 * because it needs copy only a small part of the object.
	 */
	[[gnu::pure]]
	std::string_view RelativeToApplied(AllocatorPtr alloc,
					   const LhttpAddress &apply_base,
					   std::string_view relative) const;

	/**
	 * Does this address need to be expanded with lhttp_address_expand()?
	 */
	[[gnu::pure]]
	bool IsExpandable() const noexcept {
		return options.IsExpandable() ||
			expand_uri ||
			args.IsExpandable();
	}

	void Expand(AllocatorPtr alloc, const MatchData &match_data) noexcept;

	/**
	 * Throws std::runtime_error on error.
	 */
	void CopyTo(PreparedChildProcess &dest) const noexcept;
};
