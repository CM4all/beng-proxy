// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#pragma once

#include <exception>

#include <stddef.h>
#include <stdint.h>

struct pool;
class UnusedIstreamPtr;
class EventLoop;
class NfsCache;
class NfsStock;
struct NfsCacheHandle;
class CancellablePointer;
struct statx;
struct AllocatorStats;

class NfsCacheHandler {
public:
	virtual void OnNfsCacheResponse(NfsCacheHandle &handle,
					const struct statx &st) noexcept = 0;
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

[[gnu::pure]]
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
