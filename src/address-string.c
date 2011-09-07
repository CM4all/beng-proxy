/*
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "address-string.h"
#include "address-envelope.h"
#include "pool.h"

#include <socket/resolver.h>

#include <netdb.h>
#include <string.h>
#include <sys/un.h>

static struct address_envelope *
address_envelope_sun(struct pool *pool, const char *path)
{
    struct sockaddr_un *sun;

    size_t length = sizeof(*sun) - sizeof(sun->sun_path) + strlen(path) + 1;
    struct address_envelope *envelope =
        p_malloc(pool, sizeof(*envelope) - sizeof(envelope->address) + length);
    envelope->length = length;

    sun = (struct sockaddr_un *)&envelope->address;
    sun->sun_family = AF_UNIX;
    strcpy(sun->sun_path, path);

    return envelope;
}

struct address_envelope *
address_envelope_parse(struct pool *pool, const char *p, int default_port)
{
    if (*p == '/')
        return address_envelope_sun(pool, p);

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

