/*
 * Copyright 2007-2017 Content Management AG
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

#include "lhttp_address.hxx"
#include "pool/pool.hxx"
#include "pool/StringBuilder.hxx"
#include "AllocatorPtr.hxx"
#include "uri/Base.hxx"
#include "uri/Relative.hxx"
#include "uri/Escape.hxx"
#include "uri/Extract.hxx"
#include "puri_edit.hxx"
#include "puri_relative.hxx"
#include "pexpand.hxx"
#include "spawn/Prepared.hxx"
#include "util/StringView.hxx"

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
			 StringView new_args,
			 StringView path_info) const noexcept
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
LhttpAddress::SaveBase(AllocatorPtr alloc, const char *suffix) const noexcept
{
	assert(suffix != nullptr);

	size_t length = base_string(uri, suffix);
	if (length == (size_t)-1)
		return nullptr;

	return DupWithUri(alloc, alloc.DupZ({uri, length}));
}

LhttpAddress *
LhttpAddress::LoadBase(AllocatorPtr alloc, const char *suffix) const noexcept
{
	assert(suffix != nullptr);
	assert(uri != nullptr);
	assert(*uri != 0);
	assert(uri[strlen(uri) - 1] == '/');
	assert(suffix != nullptr);

	return DupWithUri(alloc, alloc.Concat(uri, suffix));
}

const LhttpAddress *
LhttpAddress::Apply(AllocatorPtr alloc, StringView relative) const noexcept
{
	if (relative.empty())
		return this;

	if (uri_has_authority(relative))
		return nullptr;

	const char *p = uri_absolute(alloc, path, relative);
	assert(p != nullptr);

	return alloc.New<LhttpAddress>(ShallowCopy(), *this, p);
}

StringView
LhttpAddress::RelativeTo(const LhttpAddress &base) const noexcept
{
	if (strcmp(base.path, path) != 0)
		return nullptr;

	return uri_relative(base.uri, uri);
}

void
LhttpAddress::Expand(AllocatorPtr alloc, const MatchInfo &match_info) noexcept
{
	options.Expand(alloc, match_info);

	if (expand_uri) {
		expand_uri = false;
		uri = expand_string(alloc, uri, match_info);
	}

	args.Expand(alloc, match_info);
}

void
LhttpAddress::CopyTo(PreparedChildProcess &dest) const noexcept
{
	dest.Append(path);

	for (const char *i : args)
		dest.Append(i);

	options.CopyTo(dest, true, nullptr);
}
