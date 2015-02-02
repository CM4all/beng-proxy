/*
 * Global declarations.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "bp_instance.hxx"
#include "http_cache.hxx"
#include "fcache.hxx"
#include "nfs_cache.hxx"

void
instance::ForkCow(bool inherit)
{
    if (http_cache != nullptr)
        http_cache_fork_cow(*http_cache, inherit);

    if (filter_cache != nullptr)
        filter_cache_fork_cow(filter_cache, inherit);

    if (nfs_cache != nullptr)
        nfs_cache_fork_cow(nfs_cache, inherit);
}
