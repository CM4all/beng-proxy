/*
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef NET_RESOLVER_HXX
#define NET_RESOLVER_HXX

struct addrinfo;
class AddressInfo;

AddressInfo
Resolve(const char *host_and_port, int default_port,
        const struct addrinfo *hints);

#endif
