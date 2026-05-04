// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <max.kellermann@ionos.com>

#include "Address.hxx"
#include "pool/StringBuilder.hxx"
#include "AllocatorPtr.hxx"
#include "uri/Base.hxx"
#include "uri/PEdit.hxx"
#include "uri/PRelative.hxx"
#include "uri/Relative.hxx"
#include "uri/Extract.hxx"
#include "util/djb_hash.hxx"
#include "util/SpanCast.hxx"
#include "util/StringAPI.hxx"
#include "pexpand.hxx"
#include "spawn/Prepared.hxx"
#include "stock/Key.hxx"

using std::string_view_literals::operator""sv;

LhttpAddress::LhttpAddress(const char *_path) noexcept
	:path(_path),
	 host_and_port(nullptr),
	 uri(nullptr)
{
	assert(path != nullptr);
}

LhttpAddress::LhttpAddress(AllocatorPtr alloc,
			   const LhttpAddress &src) noexcept
	:path(alloc.Dup(src.path)),
	 args(alloc, src.args),
	 options(alloc, src.options),
	 host_and_port(alloc.CheckDup(src.host_and_port)),
	 uri(alloc.Dup(src.uri)),
	 cached_child_id(alloc.Dup(src.cached_child_id)),
	 parallelism(src.parallelism),
	 concurrency(src.concurrency),
	 blocking(src.blocking),
	 expand_uri(src.expand_uri)
{
}

void
LhttpAddress::PostCacheStore(AllocatorPtr alloc) noexcept
{
	/* cache the GetChildId() call only if we expect future calls
           to have the same result, i.e. none of the relevant fields
           are "expandable" */
	if (!IsChildExpandable())
		cached_child_id = GetChildId(alloc);
}

inline std::size_t
LhttpAddress::BuildChildId(PoolStringBuilder<256> &b,
			   std::span<char, 16384> options_buffer) const noexcept
{
	std::size_t hash = options.GetHash();

	const std::string_view path_sv{path};
	b.push_back(path_sv);
	hash = djb_hash(AsBytes(path_sv), hash);

	for (std::string_view i : args) {
		b.push_back("!");
		b.push_back(i);
		hash = djb_hash(AsBytes(i), hash);
	}

	b.emplace_back(options_buffer.data(),
		       options.MakeId(options_buffer.data()));

	return hash;
}

StockKey
LhttpAddress::GetChildId(AllocatorPtr alloc) const noexcept
{
	if (!cached_child_id.IsNull())
		return cached_child_id;

	PoolStringBuilder<256> b;
	char child_options_buffer[16384];
	std::size_t hash = BuildChildId(b, child_options_buffer);
	return StringWithHash{b.MakeView(alloc), hash};
}

StringWithHash
LhttpAddress::GetId(AllocatorPtr alloc) const noexcept
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

	if (host_and_port != nullptr) {
		b.push_back(";h=");
		hash = djb_hash_string(host_and_port, hash);
		b.push_back(host_and_port);
	}

	if (uri != nullptr) {
		b.push_back(";u=");
		hash = djb_hash_string(uri, hash);
		b.push_back(uri);
	}

	return StringWithHash{
		b.MakeView(alloc),
		hash,
	};
}

LhttpAddress *
LhttpAddress::Dup(AllocatorPtr alloc) const noexcept
{
	return alloc.New<LhttpAddress>(alloc, *this);
}

void
LhttpAddress::Check() const
{
	if (uri == nullptr)
		throw std::runtime_error("missing LHTTP_URI");

	options.Check();
}

bool
LhttpAddress::IsSameProgram(const LhttpAddress &other) const noexcept
{
	// TODO: check args, params, options?
	return StringIsEqual(path, other.path);
}

LhttpAddress *
LhttpAddress::DupWithUri(AllocatorPtr alloc, const char *new_uri) const noexcept
{
	LhttpAddress *p = Dup(alloc);
	p->uri = new_uri;
	return p;
}

bool
LhttpAddress::HasQueryString() const noexcept
{
	return strchr(uri, '?') != nullptr;
}

LhttpAddress *
LhttpAddress::InsertQueryString(AllocatorPtr alloc,
				const char *query_string) const noexcept
{
	return alloc.New<LhttpAddress>(ShallowCopy(), *this,
				       uri_insert_query_string(alloc, uri,
							       query_string));
}

LhttpAddress *
LhttpAddress::InsertArgs(AllocatorPtr alloc,
			 std::string_view new_args,
			 std::string_view path_info) const noexcept
{
	return alloc.New<LhttpAddress>(ShallowCopy(), *this,
				       uri_insert_args(alloc, uri,
						       new_args, path_info));
}

bool
LhttpAddress::IsValidBase() const noexcept
{
	return IsExpandable() || is_base(uri);
}

LhttpAddress *
LhttpAddress::SaveBase(AllocatorPtr alloc, std::string_view suffix) const noexcept
{
	size_t length = base_string(uri, suffix);
	if (length == (size_t)-1)
		return nullptr;

	return DupWithUri(alloc, alloc.DupZ({uri, length}));
}

LhttpAddress *
LhttpAddress::LoadBase(AllocatorPtr alloc, std::string_view suffix) const noexcept
{
	assert(uri != nullptr);
	assert(*uri != 0);
	assert(uri[strlen(uri) - 1] == '/');

	return DupWithUri(alloc, alloc.Concat(uri, suffix));
}

[[gnu::pure]]
static const char *
ApplyUri(AllocatorPtr alloc, const char *base_uri,
	 std::string_view relative) noexcept
{
	if (relative.empty())
		return base_uri;

	if (UriHasAuthority(relative))
		return nullptr;

	return uri_absolute(alloc, base_uri, relative);
}

LhttpAddress *
LhttpAddress::Apply(AllocatorPtr alloc, std::string_view relative) const noexcept
{
	const char *new_uri = ApplyUri(alloc, uri, relative);
	if (new_uri == nullptr)
		return nullptr;

	return alloc.New<LhttpAddress>(ShallowCopy(), *this, new_uri);
}

std::string_view
LhttpAddress::RelativeTo(const LhttpAddress &base) const noexcept
{
	if (!IsSameProgram(base))
		return {};

	return uri_relative(base.uri, uri);
}

std::string_view
LhttpAddress::RelativeToApplied(AllocatorPtr alloc,
				const LhttpAddress &apply_base,
				std::string_view relative) const
{
	if (!IsSameProgram(apply_base))
		return {};

	const char *new_uri = ApplyUri(alloc, apply_base.uri, relative);
	if (new_uri == nullptr)
		return {};

	return new_uri;
}

void
LhttpAddress::Expand(AllocatorPtr alloc, const MatchData &match_data) noexcept
{
	options.Expand(alloc, match_data);

	if (expand_uri) {
		expand_uri = false;
		uri = expand_string(alloc, uri, match_data);
	}

	args.Expand(alloc, match_data);
}

void
LhttpAddress::CopyTo(PreparedChildProcess &dest, FdHolder &close_fds) const noexcept
{
	dest.Append(path);

	for (const char *i : args)
		dest.Append(i);

	options.CopyTo(dest, close_fds);
}
