/*
 * Copyright 2007-2017 Content Management AG
 * All rights reserved.
 *
 * author: Max Kellermann <mk.com>
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
#include "pool.hxx"
#include "AllocatorPtr.hxx"
#include "uri/uri_base.hxx"
#include "uri/uri_relative.hxx"
#include "uri/uri_escape.hxx"
#include "uri/uri_extract.hxx"
#include "puri_edit.hxx"
#include "puri_relative.hxx"
#include "pexpand.hxx"
#include "spawn/Prepared.hxx"
#include "util/StringView.hxx"

#include <string.h>

LhttpAddress::LhttpAddress(const char *_path)
    :path(_path),
     host_and_port(nullptr),
     uri(nullptr), expand_uri(nullptr),
     concurrency(1),
     blocking(true)
{
    assert(path != nullptr);
}

LhttpAddress::LhttpAddress(AllocatorPtr alloc, const LhttpAddress &src)
    :path(alloc.Dup(src.path)),
     args(alloc, src.args),
     options(alloc, src.options),
     host_and_port(alloc.CheckDup(src.host_and_port)),
     uri(alloc.Dup(src.uri)),
     expand_uri(alloc.CheckDup(src.expand_uri)),
     concurrency(src.concurrency),
     blocking(src.blocking)
{
}

const char *
LhttpAddress::GetServerId(struct pool *pool) const
{
    char child_options_buffer[16384];
    *options.MakeId(child_options_buffer) = 0;

    const char *p = p_strcat(pool, path,
                             child_options_buffer,
                             nullptr);

    for (auto i : args)
        p = p_strcat(pool, p, "!", i, nullptr);

    return p;
}

const char *
LhttpAddress::GetId(struct pool *pool) const
{
    const char *p = GetServerId(pool);

    if (uri != nullptr)
        p = p_strcat(pool, p, ";u=", uri, nullptr);

    return p;
}

LhttpAddress *
LhttpAddress::Dup(AllocatorPtr alloc) const
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
LhttpAddress::DupWithUri(AllocatorPtr alloc, const char *new_uri) const
{
    LhttpAddress *p = Dup(alloc);
    p->uri = new_uri;
    return p;
}

bool
LhttpAddress::HasQueryString() const
{
    return strchr(uri, '?') != nullptr;
}

LhttpAddress *
LhttpAddress::InsertQueryString(struct pool &pool,
                                const char *query_string) const
{
    return NewFromPool<LhttpAddress>(pool, ShallowCopy(), *this,
                                     uri_insert_query_string(&pool, uri, query_string));
}

LhttpAddress *
LhttpAddress::InsertArgs(struct pool &pool,
                         StringView new_args, StringView path_info) const
{
    return NewFromPool<LhttpAddress>(pool, ShallowCopy(), *this,
                                     uri_insert_args(&pool, uri,
                                                     new_args, path_info));
}

bool
LhttpAddress::IsValidBase() const
{
    return IsExpandable() || is_base(uri);
}

LhttpAddress *
LhttpAddress::SaveBase(AllocatorPtr alloc, const char *suffix) const
{
    assert(suffix != nullptr);

    size_t length = base_string(uri, suffix);
    if (length == (size_t)-1)
        return nullptr;

    return DupWithUri(alloc, alloc.DupZ({uri, length}));
}

LhttpAddress *
LhttpAddress::LoadBase(AllocatorPtr alloc, const char *suffix) const
{
    assert(suffix != nullptr);
    assert(uri != nullptr);
    assert(*uri != 0);
    assert(uri[strlen(uri) - 1] == '/');
    assert(suffix != nullptr);

    return DupWithUri(alloc, alloc.Concat(uri, suffix));
}

const LhttpAddress *
LhttpAddress::Apply(struct pool *pool, StringView relative) const
{
    if (relative.IsEmpty())
        return this;

    if (uri_has_authority(relative))
        return nullptr;

    const char *p = uri_absolute(*pool, path, relative);
    assert(p != nullptr);

    return NewFromPool<LhttpAddress>(*pool, ShallowCopy(), *this, p);
}

StringView
LhttpAddress::RelativeTo(const LhttpAddress &base) const
{
    if (strcmp(base.path, path) != 0)
        return nullptr;

    return uri_relative(base.uri, uri);
}

void
LhttpAddress::Expand(AllocatorPtr alloc, const MatchInfo &match_info)
{
    options.Expand(alloc, match_info);

    if (expand_uri != NULL)
        uri = expand_string(alloc, expand_uri, match_info);

    args.Expand(alloc, match_info);
}

void
LhttpAddress::CopyTo(PreparedChildProcess &dest) const
{
    dest.Append(path);

    for (const char *i : args)
        dest.Append(i);

    options.CopyTo(dest, true, nullptr);
}
