/*
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "address-string.h"
#include "address-envelope.h"
#include "pool.h"

#include <socket/resolver.h>

#include <netdb.h>
#include <string.h>

struct address_envelope *
address_envelope_parse(struct pool *pool, const char *p, int default_port,
                       bool passive)
{
    static const struct addrinfo hints = {
        .ai_flags = AI_NUMERICHOST,
        .ai_family = AF_UNSPEC,
        .ai_socktype = SOCK_STREAM,
    };
    static const struct addrinfo passive_hints = {
        .ai_flags = AI_NUMERICHOST|AI_PASSIVE,
        .ai_family = AF_UNSPEC,
        .ai_socktype = SOCK_STREAM,
    };

    struct addrinfo *ai;
    int result = socket_resolve_host_port(p, default_port,
                                          passive ? &passive_hints : &hints,
                                          &ai);
    if (result != 0)
        return NULL;

    struct address_envelope *e = p_malloc(pool, sizeof(*e) -
                                          sizeof(e->address) + ai->ai_addrlen);
    e->length = ai->ai_addrlen;
    memcpy(&e->address, ai->ai_addr, ai->ai_addrlen);

    freeaddrinfo(ai);

    return e;
}

