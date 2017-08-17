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

#ifndef BENG_PROXY_NFS_ADDRESS_HXX
#define BENG_PROXY_NFS_ADDRESS_HXX

#include "util/ConstBuffer.hxx"

#include "util/Compiler.h"

class AllocatorPtr;
class MatchInfo;

/**
 * The address of a file on a NFS server.
 */
struct NfsAddress {
    const char *server;

    const char *export_name;

    const char *path;

    /**
     * The value of #TRANSLATE_EXPAND_PATH.  Only used by the
     * translation cache.
     */
    const char *expand_path;

    const char *content_type;

    ConstBuffer<void> content_type_lookup = nullptr;

    NfsAddress(const char *_server,
               const char *_export_name, const char *_path)
        :server(_server), export_name(_export_name), path(_path),
         expand_path(nullptr), content_type(nullptr) {}

    NfsAddress(AllocatorPtr alloc, const NfsAddress &other);

    NfsAddress(const NfsAddress &) = delete;
    NfsAddress &operator=(const NfsAddress &) = delete;

    const char *GetId(struct pool *pool) const;

    /**
     * Throws std::runtime_error on error.
     */
    void Check() const;

    gcc_pure
    bool HasQueryString() const {
        return false;
    }

    gcc_pure
    bool IsValidBase() const;

    NfsAddress *SaveBase(AllocatorPtr alloc, const char *suffix) const;

    NfsAddress *LoadBase(AllocatorPtr alloc, const char *suffix) const;

    /**
     * Does this address need to be expanded with Expand()?
     */
    gcc_pure
    bool IsExpandable() const {
        return expand_path != nullptr;
    }

    /**
     * Throws std::runtime_error on error.
     */
    const NfsAddress *Expand(AllocatorPtr alloc,
                             const MatchInfo &match_info) const;
};

#endif
