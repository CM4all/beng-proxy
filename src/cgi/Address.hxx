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

#include "spawn/ChildOptions.hxx"
#include "cluster/AddressList.hxx"
#include "adata/ExpandableStringList.hxx"

#include "util/Compiler.h"

class AllocatorPtr;
class MatchInfo;

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

	bool expand_path = false;
	bool expand_uri = false;
	bool expand_script_name = false;
	bool expand_path_info = false;
	bool expand_document_root = false;

	explicit CgiAddress(const char *_path);

	constexpr CgiAddress(ShallowCopy shallow_copy, const CgiAddress &src)
		:path(src.path),
		 args(shallow_copy, src.args), params(shallow_copy, src.params),
		 options(shallow_copy, src.options),
		 interpreter(src.interpreter), action(src.action),
		 uri(src.uri), script_name(src.script_name), path_info(src.path_info),
		 query_string(src.query_string), document_root(src.document_root),
		 address_list(shallow_copy, src.address_list),
		 expand_path(src.expand_path),
		 expand_uri(src.expand_uri),
		 expand_script_name(src.expand_script_name),
		 expand_path_info(src.expand_path_info),
		 expand_document_root(src.expand_document_root)
	{
	}

	constexpr CgiAddress(CgiAddress &&src):CgiAddress(ShallowCopy(), src) {}

	CgiAddress(AllocatorPtr alloc, const CgiAddress &src);

	CgiAddress &operator=(const CgiAddress &) = delete;

	gcc_pure
	const char *GetURI(AllocatorPtr alloc) const;

	/**
	 * Generates a string identifying the address.  This can be used as a
	 * key in a hash table.  The string will be allocated by the specified
	 * pool.
	 */
	gcc_pure
	const char *GetId(AllocatorPtr alloc) const;

	void Check() const {
		options.Check();
	}

	gcc_pure
	bool HasQueryString() const {
		return query_string != nullptr && *query_string != 0;
	}

	void InsertQueryString(AllocatorPtr alloc, const char *new_query_string);

	void InsertArgs(AllocatorPtr alloc, StringView new_args,
			StringView new_path_info);

	CgiAddress *Clone(AllocatorPtr alloc) const;

	gcc_pure
	bool IsValidBase() const;

	const char *AutoBase(AllocatorPtr alloc, const char *request_uri) const;

	CgiAddress *SaveBase(AllocatorPtr alloc, const char *suffix) const;

	CgiAddress *LoadBase(AllocatorPtr alloc, const char *suffix) const;

	/**
	 * @return a new object on success, src if no change is needed,
	 * nullptr on error
	 */
	const CgiAddress *Apply(AllocatorPtr alloc, StringView relative) const;

	/**
	 * Does this address need to be expanded with Expand()?
	 */
	gcc_pure
	bool IsExpandable() const {
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
	void Expand(AllocatorPtr alloc, const MatchInfo &match_info);
};
