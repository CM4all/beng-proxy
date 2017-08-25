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

#ifndef BENG_PROXY_LHTTP_ADDRESS_HXX
#define BENG_PROXY_LHTTP_ADDRESS_HXX

#include "spawn/ChildOptions.hxx"
#include "adata/ExpandableStringList.hxx"
#include "util/ShallowCopy.hxx"

#include "util/Compiler.h"

#include <assert.h>

class AllocatorPtr;
struct StringView;

/**
 * The address of a HTTP server that is launched and managed by
 * beng-proxy.
 */
struct LhttpAddress {
    const char *path;

    ExpandableStringList args;

    ChildOptions options;

    /**
     * The host part of the URI (including the port, if any).
     */
    const char *host_and_port;

    const char *uri;

    /**
     * The value of #TRANSLATE_EXPAND_PATH.  Only used by the
     * translation cache.
     */
    const char *expand_uri;

    /**
     * The maximum number of concurrent connections to one instance.
     */
    unsigned concurrency;

    /**
     * Pass a blocking listener socket to the child process?  The
     * default is true; sets SOCK_NONBLOCK if false.
     */
    bool blocking;

    explicit LhttpAddress(const char *path);

    constexpr LhttpAddress(ShallowCopy shallow_copy, const LhttpAddress &src)
        :path(src.path),
         args(shallow_copy, src.args),
         options(shallow_copy, src.options),
         host_and_port(src.host_and_port),
         uri(src.uri), expand_uri(src.expand_uri),
         concurrency(src.concurrency),
         blocking(src.blocking)
    {
    }

    constexpr LhttpAddress(LhttpAddress &&src)
        :LhttpAddress(ShallowCopy(), src) {}

    LhttpAddress(ShallowCopy shallow_copy, const LhttpAddress &src,
                 const char *_uri)
        :LhttpAddress(shallow_copy, src)
    {
        uri = _uri;
    }

    LhttpAddress(AllocatorPtr alloc, const LhttpAddress &src);

    LhttpAddress &operator=(const LhttpAddress &) = delete;

    /**
     * Generates a string identifying the server process.  This can be
     * used as a key in a hash table.  The string will be allocated by
     * the specified pool.
     */
    gcc_pure
    const char *GetServerId(struct pool *pool) const;

    /**
     * Generates a string identifying the address.  This can be used as a
     * key in a hash table.  The string will be allocated by the specified
     * pool.
     */
    gcc_pure
    const char *GetId(struct pool *pool) const;

    /**
     * Throws std::runtime_error on error.
     */
    void Check() const;

    gcc_pure
    bool HasQueryString() const;

    LhttpAddress *Dup(AllocatorPtr alloc) const;

    LhttpAddress *DupWithUri(AllocatorPtr alloc, const char *uri) const;

    /**
     * Duplicates this #lhttp_address object and inserts the specified
     * query string into the URI.
     */
    gcc_malloc
    LhttpAddress *InsertQueryString(struct pool &pool,
                                    const char *query_string) const;

    /**
     * Duplicates this #lhttp_address object and inserts the specified
     * arguments into the URI.
     */
    gcc_malloc
    LhttpAddress *InsertArgs(struct pool &pool,
                             StringView new_args, StringView path_info) const;

    gcc_pure
    bool IsValidBase() const;

    LhttpAddress *SaveBase(AllocatorPtr alloc, const char *suffix) const;

    LhttpAddress *LoadBase(AllocatorPtr alloc, const char *suffix) const;

    /**
     * @return a new object on success, src if no change is needed, nullptr
     * on error
     */
    const LhttpAddress *Apply(struct pool *pool, StringView relative) const;

    gcc_pure
    StringView RelativeTo(const LhttpAddress &base) const;

    /**
     * Does this address need to be expanded with lhttp_address_expand()?
     */
    gcc_pure
    bool IsExpandable() const {
        return options.IsExpandable() ||
            expand_uri != nullptr ||
            args.IsExpandable();
    }

    void Expand(AllocatorPtr alloc, const MatchInfo &match_info);

    /**
     * Throws std::runtime_error on error.
     */
    void CopyTo(PreparedChildProcess &dest) const;
};

#endif
