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

#include "Address.hxx"
#include "pool/StringBuilder.hxx"
#include "AllocatorPtr.hxx"
#include "uri/Base.hxx"
#include "uri/PEdit.hxx"
#include "uri/PRelative.hxx"
#include "uri/Relative.hxx"
#include "uri/Extract.hxx"
#include "pexpand.hxx"
#include "spawn/Prepared.hxx"

#include <string.h>

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
	 parallelism(src.parallelism),
	 concurrency(src.concurrency),
	 blocking(src.blocking),
	 expand_uri(src.expand_uri)
{
}

const char *
LhttpAddress::GetServerId(AllocatorPtr alloc) const noexcept
{
	PoolStringBuilder<256> b;
	b.push_back(path);

	char child_options_buffer[16384];
	b.emplace_back(child_options_buffer,
		       options.MakeId(child_options_buffer));

	for (auto i : args) {
		b.push_back("!");
		b.push_back(i);
	}

	return b(alloc);
}

const char *
LhttpAddress::GetId(AllocatorPtr alloc) const noexcept
{
	const char *p = GetServerId(alloc);

	if (uri != nullptr)
		p = alloc.Concat(p, ";u=", uri);

	return p;
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
	return strcmp(path, other.path) == 0;
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

const LhttpAddress *
LhttpAddress::Apply(AllocatorPtr alloc, std::string_view relative) const noexcept
{
	if (relative.empty())
		return this;

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
LhttpAddress::CopyTo(PreparedChildProcess &dest) const noexcept
{
	dest.Append(path);

	for (const char *i : args)
		dest.Append(i);

	options.CopyTo(dest);
}
