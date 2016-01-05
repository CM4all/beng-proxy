/*
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "NameCache.hxx"
#include "certdb/Config.hxx"

CertNameCache::CertNameCache(const CertDatabaseConfig &config)
    :schema(config.schema)
{
}

bool
CertNameCache::Lookup(const char *host) const
{
    if (!complete)
        /* we can't give reliable results until the cache is
           complete */
        return true;

    const std::unique_lock<std::mutex> lock(mutex);
    return names.find(host) != names.end();
}
