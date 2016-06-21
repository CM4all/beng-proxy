/*
 * Store a URI along with a list of socket addresses.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "http_address.hxx"
#include "shm/dpool.hxx"

HttpAddress::HttpAddress(struct dpool &pool, const HttpAddress &src)
    :protocol(src.protocol), ssl(src.ssl),
     host_and_port(d_strdup_checked(pool, src.host_and_port)),
     path(d_strdup(pool, src.path)),
     expand_path(d_strdup_checked(pool, src.expand_path)),
     addresses(pool, src.addresses)
{
}

void
HttpAddress::Free(struct dpool &pool)
{
    if (host_and_port != nullptr)
        d_free(pool, host_and_port);
    if (path != nullptr)
        d_free(pool, path);
    if (expand_path != nullptr)
        d_free(pool, expand_path);
    addresses.Free(pool);
}
