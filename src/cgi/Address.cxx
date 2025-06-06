// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <max.kellermann@ionos.com>

#include "Address.hxx"
#include "pool/pool.hxx"
#include "pool/tpool.hxx"
#include "pool/StringBuilder.hxx"
#include "AllocatorPtr.hxx"
#include "uri/Base.hxx"
#include "uri/Unescape.hxx"
#include "uri/Extract.hxx"
#include "uri/Compare.hxx"
#include "uri/Relative.hxx"
#include "uri/PEdit.hxx"
#include "uri/PEscape.hxx"
#include "uri/PRelative.hxx"
#include "util/djb_hash.hxx"
#include "util/SpanCast.hxx"
#include "util/StringAPI.hxx"
#include "util/StringWithHash.hxx"
#include "pexpand.hxx"

#include <stdexcept>

CgiAddress::CgiAddress(AllocatorPtr alloc, const CgiAddress &src) noexcept
	:path(alloc.Dup(src.path)),
	 args(alloc, src.args),
	 params(alloc, src.params),
	 options(alloc, src.options),
	 interpreter(alloc.CheckDup(src.interpreter)),
	 action(alloc.CheckDup(src.action)),
	 uri(alloc.CheckDup(src.uri)),
	 script_name(alloc.CheckDup(src.script_name)),
	 path_info(alloc.CheckDup(src.path_info)),
	 query_string(alloc.CheckDup(src.query_string)),
	 document_root(alloc.CheckDup(src.document_root)),
	 address_list(alloc, src.address_list),
	 cached_child_id(alloc.Dup(src.cached_child_id)),
	 parallelism(src.parallelism),
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

void
CgiAddress::PostCacheStore(AllocatorPtr alloc) noexcept
{
	/* cache the GetChildId() call only if we expect future calls
           to have the same result, i.e. none of the relevant fields
           are "expandable" */
	if ((action != nullptr || !expand_path) && !IsChildExpandable())
		cached_child_id = GetChildId(alloc);
}

[[gnu::pure]]
static bool
HasTrailingSlash(const char *p) noexcept
{
	size_t length = strlen(p);
	return length > 0 && p[length - 1] == '/';
}

const char *
CgiAddress::GetURI(AllocatorPtr alloc) const noexcept
{
	if (uri != nullptr)
		return uri;

	const char *sn = script_name;
	if (sn == nullptr)
		sn = "/";

	std::string_view pi = GetPathInfo();
	const char *qm = "";
	const char *qs = query_string;

	if (pi.empty()) {
		if (qs == nullptr)
			return sn;
	}

	if (qs != nullptr)
		qm = "?";
	else
		qs = "";

	if (pi.starts_with('/') && HasTrailingSlash(sn))
		/* avoid generating a double slash when concatenating
		   script_name and path_info */
		pi.remove_prefix(1);

	return alloc.Concat(sn, pi, qm, qs);
}

inline std::size_t
CgiAddress::BuildChildId(PoolStringBuilder<256> &b,
			 std::span<char, 16384> options_buffer) const noexcept
{
	std::size_t hash = options.GetHash();

	{
		const std::string_view value{action != nullptr ? action : path};
		b.push_back(value);
		hash = djb_hash(AsBytes(value), hash);
	}

	for (std::string_view i : args) {
		b.push_back("!");
		b.push_back(i);
		hash = djb_hash(AsBytes(i), hash);
	}

	for (std::string_view i : options.env) {
		b.push_back("$");
		b.push_back(i);
		hash = djb_hash(AsBytes(i), hash);
	}

	b.emplace_back(options_buffer.data(),
		       options.MakeId(options_buffer.data()));

	return hash;
}

StringWithHash
CgiAddress::GetChildId(AllocatorPtr alloc) const noexcept
{
	if (!cached_child_id.IsNull())
		return cached_child_id;

	PoolStringBuilder<256> b;

	char child_options_buffer[16384];
	std::size_t hash = BuildChildId(b, child_options_buffer);

	return StringWithHash{b.MakeView(alloc), hash};
}

StringWithHash
CgiAddress::GetId(AllocatorPtr alloc) const noexcept
{
	PoolStringBuilder<256> b;
	std::size_t hash;

	char child_options_buffer[16384];
	if (cached_child_id.IsNull()) {
		hash = BuildChildId(b, child_options_buffer);
	} else {
		/* thie first part of the id (the part that is
                   specific to the child process) was already
                   calculated, so let's use that */
		hash = cached_child_id.hash;
		b.push_back(cached_child_id.value);
	}

	if (action != nullptr) {
		b.push_back(";p=");
		hash = djb_hash_string(path, hash);
		b.push_back(path);
	}

	if (document_root != nullptr) {
		b.push_back(";d=");
		b.push_back(document_root);
	}

	if (interpreter != nullptr) {
		b.push_back(";i=");
		const std::string_view value{interpreter};
		b.push_back(value);
		hash = djb_hash(AsBytes(value), hash);
	}

	for (std::string_view i : params) {
		b.push_back("!");
		b.push_back(i);
		hash = djb_hash(AsBytes(i), hash);
	}

	if (uri != nullptr) {
		b.push_back(";u=");
		const std::string_view value{uri};
		b.push_back(value);
		hash = djb_hash(AsBytes(value), hash);
	} else if (script_name != nullptr) {
		b.push_back(";s=");
		const std::string_view value{script_name};
		b.push_back(value);
		hash = djb_hash(AsBytes(value), hash);
	}

	if (path_info != nullptr) {
		b.push_back(";p=");
		b.push_back(path_info);
		const std::string_view value{path_info};
		b.push_back(value);
		hash = djb_hash(AsBytes(value), hash);
	}

	if (query_string != nullptr) {
		b.push_back("?");
		const std::string_view value{query_string};
		b.push_back(value);
		hash = djb_hash(AsBytes(value), hash);
	}

	return StringWithHash{
		b.MakeView(alloc),
		hash,
	};
}

void
CgiAddress::Check(bool is_was) const
{
	if (is_was) {
		if (!address_list.empty()) {
			if (concurrency == 0)
				throw std::runtime_error("Missing concurrency for Remote-WAS");

			if (!address_list.IsSingle())
				throw std::runtime_error("Too many Remote-WAS addresses");

			if (address_list.front().GetFamily() != AF_LOCAL)
				throw std::runtime_error("Remote-WAS requires AF_LOCAL");
		}
	}

	options.Check();
}

CgiAddress *
CgiAddress::Clone(AllocatorPtr alloc) const noexcept
{
	return alloc.New<CgiAddress>(alloc, *this);
}

bool
CgiAddress::IsSameProgram(const CgiAddress &other) const noexcept
{
	// TODO: check args, params, options?
	return StringIsEqual(path, other.path);
}

bool
CgiAddress::IsSameBase(const CgiAddress &other) const noexcept
{
	return IsSameProgram(other) &&
		StringIsEqual(script_name != nullptr ? script_name : "",
			      other.script_name != nullptr ? other.script_name : "");
}

void
CgiAddress::InsertQueryString(AllocatorPtr alloc,
			      const char *new_query_string) noexcept
{
	if (query_string != nullptr)
		query_string = alloc.Concat(new_query_string, "&", query_string);
	else
		query_string = alloc.Dup(new_query_string);
}

void
CgiAddress::InsertArgs(AllocatorPtr alloc, std::string_view new_args,
		       std::string_view new_path_info) noexcept
{
	if (uri != nullptr)
		uri = uri_insert_args(alloc, uri, new_args, new_path_info);

	if (path_info != nullptr)
		path_info = alloc.Concat(path_info,
					 ';', new_args,
					 new_path_info);
}

bool
CgiAddress::IsValidBase() const noexcept
{
	if (IsExpandable())
		return true;

	const auto pi = GetPathInfo();
	if (pi.empty())
		return script_name != nullptr && is_base(script_name);
	else
		return is_base(pi);
}

const char *
CgiAddress::AutoBase(AllocatorPtr alloc,
		     const char *request_uri) const noexcept
{
	auto pi = GetPathInfo();

	/* XXX implement (un-)escaping of the uri */

	/* either SCRIPT_NAME must end with a slash or PATH_INFO must
	   start with one */
	if (script_name == nullptr || !is_base(script_name)) {
		if (!pi.starts_with('/'))
			return nullptr;

		pi.remove_prefix(1);
	}

	size_t length = base_string(request_uri, pi);
	if (length == 0 || length == (size_t)-1)
		return nullptr;

	return alloc.DupZ({request_uri, length});
}

CgiAddress *
CgiAddress::SaveBase(AllocatorPtr alloc, std::string_view suffix) const noexcept
{
	size_t uri_length = 0;
	if (uri != nullptr) {
		const char *end = UriFindUnescapedSuffix(uri, suffix);
		if (end == nullptr)
			return nullptr;

		uri_length = end - uri;
	}

	std::string_view new_path_info = GetPathInfo();
	const char *new_path_info_end =
		UriFindUnescapedSuffix(new_path_info, suffix);
	if (new_path_info_end == nullptr)
		return nullptr;

	CgiAddress *dest = Clone(alloc);
	if (dest->uri != nullptr)
		dest->uri = alloc.DupZ({dest->uri, uri_length});
	dest->path_info = alloc.DupZ({new_path_info.data(), new_path_info_end});
	return dest;
}

CgiAddress *
CgiAddress::LoadBase(AllocatorPtr alloc, std::string_view suffix) const noexcept
{
	const TempPoolLease tpool;

	char *unescaped = uri_unescape_dup(*tpool, suffix);
	if (unescaped == nullptr)
		return nullptr;

	CgiAddress *dest = Clone(alloc);
	if (dest->uri != nullptr)
		dest->uri = alloc.Concat(dest->uri, unescaped);

	dest->path_info = alloc.Concat(GetPathInfo(), unescaped);
	return dest;
}

[[gnu::pure]]
static const char *
UnescapeApplyPathInfo(AllocatorPtr alloc, const char *base_path_info,
		      std::string_view relative_escaped) noexcept
{
	if (base_path_info == nullptr)
		base_path_info = "";

	if (relative_escaped.empty())
		return base_path_info;

	if (UriHasAuthority(relative_escaped))
		return nullptr;

	const TempPoolLease tpool;

	char *unescaped = (char *)p_malloc(tpool, relative_escaped.size());
	char *unescaped_end = UriUnescape(unescaped, relative_escaped);
	if (unescaped_end == nullptr)
		return nullptr;

	size_t unescaped_length = unescaped_end - unescaped;

	return uri_absolute(alloc, base_path_info,
			    {unescaped, unescaped_length});
}

CgiAddress *
CgiAddress::Apply(AllocatorPtr alloc,
		  std::string_view relative) const noexcept
{
	const char *new_path_info = UnescapeApplyPathInfo(alloc, path_info,
							  relative);
	if (new_path_info == nullptr)
		return nullptr;

	auto *dest = alloc.New<CgiAddress>(ShallowCopy(), *this);
	dest->path_info = new_path_info;
	return dest;
}

std::string_view
CgiAddress::RelativeTo(const CgiAddress &base) const noexcept
{
	if (!IsSameProgram(base))
		return {};

	if (path_info == nullptr || base.path_info == nullptr)
		return {};

	return uri_relative(base.path_info, path_info);
}

std::string_view
CgiAddress::RelativeToApplied(AllocatorPtr alloc,
			      const CgiAddress &apply_base,
			      std::string_view relative) const noexcept
{
	if (!IsSameProgram(apply_base))
		return {};

	if (path_info == nullptr)
		return {};

	const char *new_path_info =
		UnescapeApplyPathInfo(alloc, apply_base.path_info, relative);
	if (new_path_info == nullptr)
		return {};

	return uri_relative(path_info, new_path_info);
}

void
CgiAddress::Expand(AllocatorPtr alloc, const MatchData &match_data)
{
	options.Expand(alloc, match_data);

	if (expand_path) {
		expand_path = false;
		path = expand_string_unescaped(alloc, path, match_data);
	}

	if (expand_uri) {
		expand_uri = false;
		uri = expand_string_unescaped(alloc, uri, match_data);
	}

	if (expand_script_name) {
		expand_script_name = false;
		script_name = expand_string_unescaped(alloc, script_name, match_data);
	}

	if (expand_path_info) {
		expand_path_info = false;
		path_info = expand_string_unescaped(alloc, path_info, match_data);
	}

	if (expand_document_root) {
		expand_document_root = false;
		document_root = expand_string_unescaped(alloc, document_root,
							match_data);
	}

	args.Expand(alloc, match_data);
	params.Expand(alloc, match_data);
}
