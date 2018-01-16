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

#include "cgi_address.hxx"
#include "pool/pool.hxx"
#include "AllocatorPtr.hxx"
#include "uri/uri_base.hxx"
#include "uri/uri_escape.hxx"
#include "uri/uri_extract.hxx"
#include "util/StringView.hxx"
#include "puri_relative.hxx"
#include "puri_base.hxx"
#include "puri_escape.hxx"
#include "puri_edit.hxx"
#include "pexpand.hxx"

#include <string.h>

CgiAddress::CgiAddress(const char *_path)
    :path(_path)
{
}

CgiAddress::CgiAddress(AllocatorPtr alloc, const CgiAddress &src)
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
     expand_path(alloc.CheckDup(src.expand_path)),
     expand_uri(alloc.CheckDup(src.expand_uri)),
     expand_script_name(alloc.CheckDup(src.expand_script_name)),
     expand_path_info(alloc.CheckDup(src.expand_path_info)),
     expand_document_root(alloc.CheckDup(src.expand_document_root)),
     address_list(alloc, src.address_list)
{
}

gcc_pure
static bool
HasTrailingSlash(const char *p)
{
    size_t length = strlen(p);
    return length > 0 && p[length - 1] == '/';
}

const char *
CgiAddress::GetURI(struct pool *pool) const
{
    if (uri != nullptr)
        return uri;

    const char *sn = script_name;
    if (sn == nullptr)
        sn = "/";

    const char *pi = path_info;
    const char *qm = nullptr;
    const char *qs = query_string;

    if (pi == nullptr) {
        if (qs == nullptr)
            return sn;

        pi = "";
    }

    if (qs != nullptr)
        qm = "?";

    if (*pi == '/' && HasTrailingSlash(sn))
        /* avoid generating a double slash when concatenating
           script_name and path_info */
        ++pi;

    return p_strcat(pool, sn, pi, qm, qs, nullptr);
}

const char *
CgiAddress::GetId(struct pool *pool) const
{
    char child_options_buffer[16384];
    *options.MakeId(child_options_buffer) = 0;

    const char *p = p_strcat(pool, path,
                             child_options_buffer,
                             nullptr);

    if (document_root != nullptr)
        p = p_strcat(pool, p, ";d=", document_root, nullptr);

    if (interpreter != nullptr)
        p = p_strcat(pool, p, ";i=", interpreter, nullptr);

    if (action != nullptr)
        p = p_strcat(pool, p, ";a=", action, nullptr);

    for (auto i : args)
        p = p_strcat(pool, p, "!", i, nullptr);

    for (auto i : params)
        p = p_strcat(pool, p, "~", i, nullptr);

    if (uri != nullptr)
        p = p_strcat(pool, p, ";u=", uri, nullptr);
    else if (script_name != nullptr)
        p = p_strcat(pool, p, ";s=", script_name, nullptr);

    if (path_info != nullptr)
        p = p_strcat(pool, p, ";p=", path_info, nullptr);

    if (query_string != nullptr)
        p = p_strcat(pool, p, "?", query_string, nullptr);

    return p;
}

CgiAddress *
CgiAddress::Clone(AllocatorPtr alloc) const
{
    return alloc.New<CgiAddress>(alloc, *this);
}

void
CgiAddress::InsertQueryString(struct pool &pool, const char *new_query_string)
{
    if (query_string != nullptr)
        query_string = p_strcat(&pool, new_query_string, "&",
                                query_string, nullptr);
    else
        query_string = p_strdup(&pool, new_query_string);
}

void
CgiAddress::InsertArgs(struct pool &pool, StringView new_args,
                       StringView new_path_info)
{
    uri = uri_insert_args(&pool, uri, new_args, new_path_info);

    if (path_info != nullptr)
        path_info = p_strncat(&pool,
                              path_info, strlen(path_info),
                              ";", (size_t)1, new_args.data, new_args.size,
                              new_path_info.data, new_path_info.size,
                              nullptr);
}

bool
CgiAddress::IsValidBase() const
{
    return IsExpandable() || (path_info != nullptr && is_base(path_info));
}

const char *
CgiAddress::AutoBase(AllocatorPtr alloc, const char *request_uri) const
{
    /* auto-generate the BASE only if the path info begins with a
       slash and matches the URI */

    if (path_info == nullptr ||
        path_info[0] != '/' ||
        path_info[1] == 0)
        return nullptr;

    /* XXX implement (un-)escaping of the uri */

    size_t length = base_string(request_uri, path_info + 1);
    if (length == 0 || length == (size_t)-1)
        return nullptr;

    return alloc.DupZ({request_uri, length});
}

CgiAddress *
CgiAddress::SaveBase(AllocatorPtr alloc, const char *suffix) const
{
    assert(suffix != nullptr);

    size_t uri_length = uri != nullptr
        ? base_string_unescape(alloc, uri, suffix)
        : 0;
    if (uri_length == (size_t)-1)
        return nullptr;

    const char *new_path_info = path_info != nullptr ? path_info : "";
    size_t length = base_string_unescape(alloc, new_path_info, suffix);
    if (length == (size_t)-1)
        return nullptr;

    CgiAddress *dest = Clone(alloc);
    if (dest->uri != nullptr)
        dest->uri = alloc.DupZ({dest->uri, uri_length});
    dest->path_info = alloc.DupZ({new_path_info, length});
    return dest;
}

CgiAddress *
CgiAddress::LoadBase(AllocatorPtr alloc, const char *suffix) const
{
    assert(suffix != nullptr);

    char *unescaped = uri_unescape_dup(alloc, suffix);
    if (unescaped == nullptr)
        return nullptr;

    CgiAddress *dest = Clone(alloc);
    if (dest->uri != nullptr)
        dest->uri = alloc.Concat(dest->uri, unescaped);

    const char *new_path_info = path_info != nullptr ? path_info : "";
    dest->path_info = alloc.Concat(new_path_info, unescaped);
    return dest;
}

const CgiAddress *
CgiAddress::Apply(struct pool *pool,
                  StringView relative) const
{
    if (relative.empty())
        return this;

    if (uri_has_authority(relative))
        return nullptr;

    char *unescaped = (char *)p_malloc(pool, relative.size);
    char *unescaped_end = uri_unescape(unescaped, relative);
    if (unescaped_end == nullptr)
        return nullptr;

    size_t unescaped_length = unescaped_end - unescaped;

    const char *new_path_info = path_info != nullptr ? path_info : "";

    auto *dest = NewFromPool<CgiAddress>(*pool, ShallowCopy(), *this);
    dest->path_info = uri_absolute(*pool, new_path_info,
                                   {unescaped, unescaped_length});
    assert(dest->path_info != nullptr);
    return dest;
}

void
CgiAddress::Expand(AllocatorPtr alloc, const MatchInfo &match_info)
{
    options.Expand(alloc, match_info);

    if (expand_path != nullptr)
        path = expand_string_unescaped(alloc, expand_path, match_info);

    if (expand_uri != nullptr)
        uri = expand_string_unescaped(alloc, expand_uri, match_info);

    if (expand_script_name != nullptr)
        script_name = expand_string_unescaped(alloc, expand_script_name,
                                              match_info);

    if (expand_path_info != nullptr)
        path_info = expand_string_unescaped(alloc, expand_path_info,
                                            match_info);

    if (expand_document_root != nullptr)
        document_root = expand_string_unescaped(alloc, expand_document_root,
                                                match_info);

    args.Expand(alloc, match_info);
    params.Expand(alloc, match_info);
}
