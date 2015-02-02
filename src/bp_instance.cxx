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
instance_fork_cow(struct instance *instance, bool inherit)
{
    if (instance->http_cache != nullptr)
        http_cache_fork_cow(*instance->http_cache, inherit);

    if (instance->filter_cache != nullptr)
        filter_cache_fork_cow(instance->filter_cache, inherit);

    if (instance->nfs_cache != nullptr)
        nfs_cache_fork_cow(instance->nfs_cache, inherit);
}
