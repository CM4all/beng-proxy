// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#pragma once

#include "spawn/ChildOptions.hxx"
#include "cluster/AddressList.hxx"
#include "adata/ExpandableStringList.hxx"
#include "util/StringWithHash.hxx"

#include <string_view>

struct StringWithHash;
class AllocatorPtr;
class MatchData;
template<size_t MAX> class PoolStringBuilder;

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

	StringWithHash cached_child_id{nullptr};

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
	 * Set for child processes which will likely be used only
	 * once.
	 */
	bool disposable = false;

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
		 cached_child_id(src.cached_child_id),
		 concurrency(src.concurrency),
		 disposable(src.disposable),
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

	void PostCacheStore(AllocatorPtr alloc) noexcept;

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
	 * Generates a string identifying the child process.  This can
	 * be used as a key in a hash table.  The string will be
	 * allocated by the specified pool.
	 */
	[[gnu::pure]]
	StringWithHash GetChildId(AllocatorPtr alloc) const noexcept;

	/**
	 * Generates a string identifying the address.  This can be used as a
	 * key in a hash table.  The string will be allocated by the specified
	 * pool.
	 */
	[[gnu::pure]]
	StringWithHash GetId(AllocatorPtr alloc) const noexcept;

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

	void InsertArgs(AllocatorPtr alloc, std::string_view new_args,
			std::string_view new_path_info) noexcept;

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
	 * @return a new object on success, nullptr on error
	 */
	CgiAddress *Apply(AllocatorPtr alloc,
			  std::string_view relative) const noexcept;

	/**
	 * Check if this instance is relative to the base, and return the
	 * relative part.  Returns nullptr on mismatch.
	 */
	[[gnu::pure]]
	std::string_view RelativeTo(const CgiAddress &base) const noexcept;

	/**
	 * A combination of Apply() and RelativeTo(), i.e. calls
	 * apply_base.Apply(relative).RelativeTo(*this). It is cheaper
	 * because it needs copy only a small part of the object.
	 */
	[[gnu::pure]]
	std::string_view RelativeToApplied(AllocatorPtr alloc,
					   const CgiAddress &apply_base,
					   std::string_view relative) const noexcept;

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

	[[gnu::pure]]
	bool IsChildExpandable() const noexcept {
		return options.IsExpandable() ||
			args.IsExpandable();
	}

	/**
	 * Throws std::runtime_error on error.
	 */
	void Expand(AllocatorPtr alloc, const MatchData &match_data);

private:
	std::size_t BuildChildId(PoolStringBuilder<256> &b) const noexcept;
};
