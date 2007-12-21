/*
 * Socket address utilities.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "address.h"

#include <netinet/in.h>
#include <netdb.h>
#include <string.h>

static const struct sockaddr *
ipv64_normalize_mapped(const struct sockaddr *addr, socklen_t *len) {
    const struct sockaddr_in6 *a6 = (const struct sockaddr_in6 *)addr;
    static struct sockaddr_in a4;
    struct in_addr inaddr;
    u_int16_t port;

    if (addr->sa_family != AF_INET6 || !IN6_IS_ADDR_V4MAPPED(&a6->sin6_addr))
        return addr;

    memcpy(&inaddr, ((const char *)&a6->sin6_addr) + 12, sizeof(inaddr));
    port = a6->sin6_port;

    a4.sin_family = AF_INET;
    *len = sizeof(a4);
    memcpy(&a4.sin_addr, &inaddr, sizeof(inaddr));
    a4.sin_port = (in_port_t)port;

    return (const struct sockaddr *)&a4;
}                                                  

const char *
address_to_string(pool_t pool, const struct sockaddr *addr, socklen_t addrlen)
{
    int ret;
    char host[512], serv[16];

    addr = ipv64_normalize_mapped(addr, &addrlen);

    ret = getnameinfo(addr, addrlen,
                      host, sizeof(host),
                      serv, sizeof(serv),
                      NI_NUMERICHOST | NI_NUMERICSERV);
    if (ret != 0)
        return NULL;

    return p_strdup(pool, host);
}
