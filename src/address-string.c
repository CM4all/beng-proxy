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
address_envelope_parse(struct pool *pool, const char *p, int default_port)
{
    static const struct addrinfo hints = {
        .ai_flags = AI_NUMERICHOST,
    };

    struct addrinfo *ai;
    int result = socket_resolve_host_port(p, default_port, &hints, &ai);
    if (result != 0)
        return NULL;

    struct address_envelope *e = p_malloc(pool, sizeof(*e) -
                                          sizeof(e->address) + ai->ai_addrlen);
    e->length = ai->ai_addrlen;
    memcpy(&e->address, ai->ai_addr, ai->ai_addrlen);

    freeaddrinfo(ai);

    return e;
}

