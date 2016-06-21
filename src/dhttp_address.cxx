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
