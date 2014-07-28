/*
 * Global declarations.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "bp_instance.hxx"
#include "http_cache.hxx"
#include "fcache.h"

void
instance_fork_cow(struct instance *instance, bool inherit)
{
    if (instance->http_cache != NULL)
        http_cache_fork_cow(instance->http_cache, inherit);

    if (instance->filter_cache != NULL)
        filter_cache_fork_cow(instance->filter_cache, inherit);
}
