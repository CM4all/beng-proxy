/*
 * Copyright 2007-2022 CM4all GmbH
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
#include "cluster/AddressList.hxx"
#include "adata/ExpandableStringList.hxx"

#include <string_view>

class AllocatorPtr;
class MatchData;

/**
 * The address of a CGI/FastCGI/WAS request.
 */
struct CgiAddress {
	const char *path;

	/**
	 * Command-line arguments.
	 */
	ExpandableStringList args;

	/**
	 * Protocol-specific name/value pairs (per-request).
	 */
	ExpandableStringList params;

	ChildOptions options;

	const char *interpreter = nullptr;
	const char *action = nullptr;

	const char *uri = nullptr;
	const char *script_name = nullptr, *path_info = nullptr;
	const char *query_string = nullptr;
	const char *document_root = nullptr;

	/**
	 * An optional list of addresses to connect to.  If given
	 * for a FastCGI resource, then beng-proxy connects to one
	 * of the addresses instead of spawning a new child
	 * process.
	 */
	AddressList address_list;

	/**
	 * The maximum number of parallel child processes of this
	 * kind.
	 */
	unsigned parallelism = 0;

	/**
	 * The maximum number of concurrent connections to one
	 * instance.  Only applicable to WAS; if it is non-zero, then
	 * the Multi-WAS protocol is used.
	 */
	unsigned concurrency = 0;

	/**
	 * Pass the CGI parameter "REQUEST_URI" verbatim instead of
	 * building it from SCRIPT_NAME, PATH_INFO and QUERY_STRING.
	 */
	bool request_uri_verbatim = false;

	bool expand_path = false;
	bool expand_uri = false;
	bool expand_script_name = false;
	bool expand_path_info = false;
	bool expand_document_root = false;

	explicit constexpr CgiAddress(const char *_path) noexcept
		:path(_path)
	{
	}

	constexpr CgiAddress(ShallowCopy shallow_copy,
			     const CgiAddress &src) noexcept
		:path(src.path),
		 args(shallow_copy, src.args), params(shallow_copy, src.params),
		 options(shallow_copy, src.options),
		 interpreter(src.interpreter), action(src.action),
		 uri(src.uri), script_name(src.script_name), path_info(src.path_info),
		 query_string(src.query_string), document_root(src.document_root),
		 address_list(shallow_copy, src.address_list),
		 concurrency(src.concurrency),
		 request_uri_verbatim(src.request_uri_verbatim),
		 expand_path(src.expand_path),
		 expand_uri(src.expand_uri),
		 expand_script_name(src.expand_script_name),
		 expand_path_info(src.expand_path_info),
		 expand_document_root(src.expand_document_root)
	{
	}

	constexpr CgiAddress(CgiAddress &&src) noexcept
		:CgiAddress(ShallowCopy(), src) {}

	CgiAddress(AllocatorPtr alloc, const CgiAddress &src) noexcept;

	CgiAddress &operator=(const CgiAddress &) = delete;

	[[gnu::pure]]
	const char *GetURI(AllocatorPtr alloc) const noexcept;

	/**
	 * Returns the #path_info field or an empty (non-nullptr)
	 * std::string_view if there is none.
	 */
	std::string_view GetPathInfo() const noexcept {
		if (path_info == nullptr)
			return {"", 0};

		return path_info;
	}

	/**
	 * Generates a string identifying the address.  This can be used as a
	 * key in a hash table.  The string will be allocated by the specified
	 * pool.
	 */
	[[gnu::pure]]
	const char *GetId(AllocatorPtr alloc) const noexcept;

	/**
	 * Throws on error.
	 */
	void Check(bool is_was) const;

	[[gnu::pure]]
	bool IsSameProgram(const CgiAddress &other) const noexcept;

	[[gnu::pure]]
	bool IsSameBase(const CgiAddress &other) const noexcept;

	[[gnu::pure]]
	bool HasQueryString() const noexcept {
		return query_string != nullptr && *query_string != 0;
	}

	void InsertQueryString(AllocatorPtr alloc,
			       const char *new_query_string) noexcept;

	void InsertArgs(AllocatorPtr alloc, StringView new_args,
			StringView new_path_info) noexcept;

	CgiAddress *Clone(AllocatorPtr alloc) const noexcept;

	[[gnu::pure]]
	bool IsValidBase() const noexcept;

	const char *AutoBase(AllocatorPtr alloc,
			     const char *request_uri) const noexcept;

	CgiAddress *SaveBase(AllocatorPtr alloc,
			     std::string_view suffix) const noexcept;

	CgiAddress *LoadBase(AllocatorPtr alloc,
			     std::string_view suffix) const noexcept;

	/**
	 * @return a new object on success, src if no change is needed,
	 * nullptr on error
	 */
	const CgiAddress *Apply(AllocatorPtr alloc,
				StringView relative) const noexcept;

	/**
	 * Check if this instance is relative to the base, and return the
	 * relative part.  Returns nullptr on mismatch.
	 */
	[[gnu::pure]]
	StringView RelativeTo(const CgiAddress &base) const noexcept;

	/**
	 * A combination of Apply() and RelativeTo(), i.e. calls
	 * apply_base.Apply(relative).RelativeTo(*this). It is cheaper
	 * because it needs copy only a small part of the object.
	 */
	[[gnu::pure]]
	StringView RelativeToApplied(AllocatorPtr alloc,
				     const CgiAddress &apply_base,
				     StringView relative) const noexcept;

	/**
	 * Does this address need to be expanded with Expand()?
	 */
	[[gnu::pure]]
	bool IsExpandable() const noexcept {
		return options.IsExpandable() ||
			expand_path ||
			expand_uri ||
			expand_script_name ||
			expand_path_info ||
			expand_document_root ||
			args.IsExpandable() ||
			params.IsExpandable();
	}

	/**
	 * Throws std::runtime_error on error.
	 */
	void Expand(AllocatorPtr alloc, const MatchData &match_data);
};
