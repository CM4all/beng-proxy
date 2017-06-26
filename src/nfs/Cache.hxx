/*
 * A cache for NFS files.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_NFS_CACHE_HXX
#define BENG_PROXY_NFS_CACHE_HXX

#include <inline/compiler.h>

#include <exception>

#include <stddef.h>
#include <stdint.h>

struct pool;
class Istream;
class EventLoop;
struct NfsCache;
struct NfsStock;
struct NfsCacheHandle;
class CancellablePointer;
struct stat;
struct AllocatorStats;

class NfsCacheHandler {
public:
    virtual void OnNfsCacheResponse(NfsCacheHandle &handle,
                                    const struct stat &st) = 0;
    virtual void OnNfsCacheError(std::exception_ptr ep) = 0;
};

NfsCache *
nfs_cache_new(struct pool &pool, size_t max_size, NfsStock &stock,
              EventLoop &event_loop);

void
nfs_cache_free(NfsCache *cache);

gcc_pure
AllocatorStats
nfs_cache_get_stats(const NfsCache &cache);

void
nfs_cache_fork_cow(NfsCache &cache, bool inherit);

void
nfs_cache_request(struct pool &pool, NfsCache &cache,
                  const char *server, const char *export_name,
                  const char *path,
                  NfsCacheHandler &handler,
                  CancellablePointer &cancel_ptr);

Istream *
nfs_cache_handle_open(struct pool &pool, NfsCacheHandle &handle,
                      uint64_t start, uint64_t end);

#endif
