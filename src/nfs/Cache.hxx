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

#ifndef BENG_PROXY_NFS_CACHE_HXX
#define BENG_PROXY_NFS_CACHE_HXX

#include "util/Compiler.h"

#include <exception>

#include <stddef.h>
#include <stdint.h>

struct pool;
class UnusedIstreamPtr;
class EventLoop;
class NfsCache;
struct NfsStock;
struct NfsCacheHandle;
class CancellablePointer;
struct stat;
struct AllocatorStats;

class NfsCacheHandler {
public:
    virtual void OnNfsCacheResponse(NfsCacheHandle &handle,
                                    const struct stat &st) noexcept = 0;
    virtual void OnNfsCacheError(std::exception_ptr ep) noexcept = 0;
};

/**
 * A cache for NFS files.
 *
 * Throws on error.
 */
NfsCache *
nfs_cache_new(struct pool &pool, size_t max_size, NfsStock &stock,
              EventLoop &event_loop);

void
nfs_cache_free(NfsCache *cache) noexcept;

gcc_pure
AllocatorStats
nfs_cache_get_stats(const NfsCache &cache) noexcept;

void
nfs_cache_fork_cow(NfsCache &cache, bool inherit) noexcept;

void
nfs_cache_flush(NfsCache &cache) noexcept;

void
nfs_cache_request(struct pool &pool, NfsCache &cache,
                  const char *server, const char *export_name,
                  const char *path,
                  NfsCacheHandler &handler,
                  CancellablePointer &cancel_ptr) noexcept;

UnusedIstreamPtr
nfs_cache_handle_open(struct pool &pool, NfsCacheHandle &handle,
                      uint64_t start, uint64_t end) noexcept;

#endif
