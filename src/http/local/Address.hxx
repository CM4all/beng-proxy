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

#include "spawn/ChildOptions.hxx"
#include "adata/ExpandableStringList.hxx"
#include "util/ShallowCopy.hxx"

class AllocatorPtr;
struct StringView;

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
				 StringView new_args,
				 StringView path_info) const noexcept;

	[[gnu::pure]]
	bool IsValidBase() const noexcept;

	LhttpAddress *SaveBase(AllocatorPtr alloc,
			       const char *suffix) const noexcept;

	LhttpAddress *LoadBase(AllocatorPtr alloc,
			       const char *suffix) const noexcept;

	/**
	 * @return a new object on success, src if no change is needed, nullptr
	 * on error
	 */
	const LhttpAddress *Apply(AllocatorPtr alloc,
				  StringView relative) const noexcept;

	[[gnu::pure]]
	StringView RelativeTo(const LhttpAddress &base) const noexcept;

	/**
	 * A combination of Apply() and RelativeTo(), i.e. calls
	 * apply_base.Apply(relative).RelativeTo(*this). It is cheaper
	 * because it needs copy only a small part of the object.
	 */
	[[gnu::pure]]
	StringView RelativeToApplied(AllocatorPtr alloc,
				     const LhttpAddress &apply_base,
				     StringView relative) const;

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
