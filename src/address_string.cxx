/*
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "address_string.hxx"
#include "address_envelope.hxx"
#include "address_quark.h"
#include "pool.h"

#include <socket/resolver.h>

#include <assert.h>
#include <netdb.h>
#include <string.h>
#include <sys/un.h>

static struct address_envelope *
address_envelope_sun(struct pool *pool, const char *path)
{
    struct sockaddr_un *sun;

    const size_t path_length = strlen(path);
    size_t length = sizeof(*sun) - sizeof(sun->sun_path) + path_length;
    struct address_envelope *envelope = (struct address_envelope *)
        p_malloc(pool, sizeof(*envelope) - sizeof(envelope->address) + length + 1);
    envelope->length = length;

    sun = (struct sockaddr_un *)&envelope->address;
    sun->sun_family = AF_UNIX;
    memcpy(sun->sun_path, path, path_length + 1);

    return envelope;
}

struct address_envelope *
address_envelope_parse(struct pool *pool, const char *p, int default_port,
                       bool passive, GError **error_r)
{
    if (*p == '/')
        return address_envelope_sun(pool, p);

    if (*p == '@') {
#ifdef __linux
        /* abstract unix domain socket */

        struct address_envelope *envelope = address_envelope_sun(pool, p);
        assert(envelope != nullptr);

        /* replace the '@' with a null byte to make it "abstract" */
        struct sockaddr_un *sun = (struct sockaddr_un *)&envelope->address;
        assert(sun->sun_path[0] == '@');
        sun->sun_path[0] = '\0';
        return envelope;
#else
        /* Linux specific feature */
        g_set_error_literal(error_r, resolver_quark(), 0,
                            "Abstract sockets supported only on Linux");
        return nullptr;
#endif
    }

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
    if (result != 0) {
        g_set_error(error_r, resolver_quark(), result,
                    "Failed to resolve '%s': %s",
                    p, gai_strerror(result));
        return nullptr;
    }

    struct address_envelope *e = (struct address_envelope *)
        p_malloc(pool, sizeof(*e) - sizeof(e->address) + ai->ai_addrlen);
    e->length = ai->ai_addrlen;
    memcpy(&e->address, ai->ai_addr, ai->ai_addrlen);

    freeaddrinfo(ai);

    return e;
}

