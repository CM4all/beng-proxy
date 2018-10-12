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

#include "Address.hxx"
#include "uri/uri_base.hxx"
#include "uri/Compare.hxx"
#include "puri_escape.hxx"
#include "pexpand.hxx"
#include "AllocatorPtr.hxx"
#include "util/StringView.hxx"

#include <stdexcept>

#include <assert.h>
#include <string.h>

NfsAddress::NfsAddress(AllocatorPtr alloc, const NfsAddress &other)
    :server(alloc.Dup(other.server)),
     export_name(alloc.Dup(other.export_name)),
     path(alloc.Dup(other.path)),
     content_type(alloc.CheckDup(other.content_type)),
     content_type_lookup(alloc.Dup(other.content_type_lookup)),
     expand_path(other.expand_path) {}

const char *
NfsAddress::GetId(struct pool *pool) const
{
    assert(server != nullptr);
    assert(export_name != nullptr);
    assert(path != nullptr);

    return p_strcat(pool, server, ":", export_name, ":", path, nullptr);
}

void
NfsAddress::Check() const
{
    if (export_name == nullptr || *export_name == 0)
        throw std::runtime_error("missing NFS_EXPORT");

    if (path == nullptr || *path == 0)
        throw std::runtime_error("missing NFS PATH");
}

bool
NfsAddress::IsValidBase() const
{
    return IsExpandable() || is_base(path);
}

NfsAddress *
NfsAddress::SaveBase(AllocatorPtr alloc, const char *suffix) const
{
    assert(suffix != nullptr);

    const char *end = UriFindUnescapedSuffix(path, suffix);
    if (end == nullptr)
        return nullptr;

    auto dest = alloc.New<NfsAddress>(alloc.Dup(server),
                                      alloc.Dup(export_name),
                                      alloc.DupZ({path, end}));
    dest->content_type = alloc.CheckDup(content_type);
    return dest;
}

NfsAddress *
NfsAddress::LoadBase(AllocatorPtr alloc, const char *suffix) const
{
    assert(path != nullptr);
    assert(*path != 0);
    assert(path[strlen(path) - 1] == '/');
    assert(suffix != nullptr);

    char *new_path = uri_unescape_concat(alloc, path, suffix);
    if (new_path == nullptr)
        return nullptr;

    auto dest = alloc.New<NfsAddress>(alloc.Dup(server),
                                      alloc.Dup(export_name),
                                      new_path);
    dest->content_type = alloc.CheckDup(content_type);
    return dest;
}

const NfsAddress *
NfsAddress::Expand(AllocatorPtr alloc, const MatchInfo &match_info) const
{
    if (!expand_path)
        return this;

    const char *new_path = expand_string_unescaped(alloc, path,
                                                   match_info);

    auto dest = alloc.New<NfsAddress>(server, export_name, new_path);
    dest->content_type = alloc.CheckDup(content_type);
    return dest;
}
