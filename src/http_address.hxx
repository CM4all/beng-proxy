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

#ifndef BENG_PROXY_HTTP_ADDRESS_HXX
#define BENG_PROXY_HTTP_ADDRESS_HXX

#include "address_list.hxx"

#include "util/Compiler.h"

#include <stddef.h>

struct dpool;
struct StringView;
class AllocatorPtr;
class MatchInfo;

/**
 * The address of a resource stored on a HTTP server.
 */
struct HttpAddress {
    const bool ssl;

    /**
     * The value of #TRANSLATE_EXPAND_PATH.  Only used by the
     * translation cache.
     */
    bool expand_path = false;

    /**
     * The name of the SSL/TLS client certificate to be used.
     */
    const char *certificate = nullptr;

    /**
     * The host part of the URI (including the port, if any).  nullptr if
     * this is HTTP over UNIX domain socket.
     */
    const char *host_and_port;

    /**
     * The path component of the URI, starting with a slash.
     */
    const char *path;

    AddressList addresses;

    HttpAddress(bool _ssl,
                const char *_host_and_port, const char *_path);

    HttpAddress(ShallowCopy, bool _ssl,
                const char *_host_and_port, const char *_path,
                const AddressList &_addresses);

    constexpr HttpAddress(ShallowCopy shallow_copy, const HttpAddress &src)
        :ssl(src.ssl),
         expand_path(src.expand_path),
         certificate(src.certificate),
         host_and_port(src.host_and_port),
         path(src.path),
         addresses(shallow_copy, src.addresses)
    {
    }

    constexpr HttpAddress(HttpAddress &&src):HttpAddress(ShallowCopy(), src) {}

    HttpAddress(AllocatorPtr alloc, const HttpAddress &src);
    HttpAddress(AllocatorPtr alloc, const HttpAddress &src, const char *_path);

    constexpr HttpAddress(ShallowCopy shallow_copy, const HttpAddress &src,
                          const char *_path)
        :ssl(src.ssl),
         certificate(src.certificate),
         host_and_port(src.host_and_port),
         path(_path),
         addresses(shallow_copy, src.addresses)
    {
    }

    HttpAddress(struct dpool &dpool, const HttpAddress &src);
    void Free(struct dpool &pool);

    HttpAddress &operator=(const HttpAddress &) = delete;

    /**
     * Check if this instance is relative to the base, and return the
     * relative part.  Returns nullptr if both URIs do not match.
     */
    gcc_pure
    StringView RelativeTo(const HttpAddress &base) const;

    /**
     * Throws std::runtime_error on error.
     */
    void Check() const;

    /**
     * Build the absolute URI from this object, but use the specified path
     * instead.
     */
    gcc_malloc
    char *GetAbsoluteURI(struct pool *pool, const char *override_path) const;

    /**
     * Build the absolute URI from this object.
     */
    gcc_malloc
    char *GetAbsoluteURI(struct pool *pool) const;

    gcc_pure
    bool HasQueryString() const;

    /**
     * Duplicates this #http_address object and inserts the specified
     * query string into the URI.
     */
    gcc_malloc
    HttpAddress *InsertQueryString(struct pool &pool,
                                   const char *query_string) const;

    /**
     * Duplicates this #http_address object and inserts the specified
     * arguments into the URI.
     */
    gcc_malloc
    HttpAddress *InsertArgs(struct pool &pool,
                            StringView args, StringView path_info) const;

    gcc_pure
    bool IsValidBase() const;

    gcc_malloc
    HttpAddress *SaveBase(AllocatorPtr alloc, const char *suffix) const;

    gcc_malloc
    HttpAddress *LoadBase(AllocatorPtr alloc, const char *suffix) const;

    const HttpAddress *Apply(AllocatorPtr alloc, StringView relative) const;

    /**
     * Does this address need to be expanded with http_address_expand()?
     */
    gcc_pure
    bool IsExpandable() const {
        return expand_path;
    }

    /**
     * Throws std::runtime_error on error.
     */
    void Expand(AllocatorPtr alloc, const MatchInfo &match_info);

    gcc_pure
    int GetDefaultPort() const {
        return ssl ? 443 : 80;
    }
};

/**
 * Parse the given absolute URI into a newly allocated
 * #http_address object.
 *
 * Throws std::runtime_error on error.
 */
gcc_malloc
HttpAddress *
http_address_parse(AllocatorPtr alloc, const char *uri);

/**
 * Create a new #http_address object from the specified one, but
 * replace the "path" attribute.  The string pointers are stored,
 * they are not duplicated.
 */
gcc_malloc
HttpAddress *
http_address_with_path(AllocatorPtr alloc,
                       const HttpAddress *uwa,
                       const char *path);

/**
 * Create a new #http_address object from the specified one, but
 * replace the "path" attribute.  The strings from the source object
 * are duplicated, but the "path" parameter is not.
 */
gcc_malloc
HttpAddress *
http_address_dup_with_path(AllocatorPtr alloc,
                           const HttpAddress *uwa,
                           const char *path);

#endif
