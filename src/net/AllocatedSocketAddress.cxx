/*
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "AllocatedSocketAddress.hxx"
#include "address_quark.h"

#include <socket/resolver.h>

#include <glib.h>

#include <assert.h>
#include <sys/un.h>
#include <netdb.h>
#include <string.h>

AllocatedSocketAddress::AllocatedSocketAddress(SocketAddress _address)
    :AllocatedSocketAddress()
{
    assert(!_address.IsNull());

    SetSize(_address.GetSize());
    memcpy(address, _address.GetAddress(), size);
}

void
AllocatedSocketAddress::SetSize(size_t new_size)
{
    if (size == new_size)
        return;

    free(address);
    size = new_size;
    address = (struct sockaddr *)malloc(size);
}

void
AllocatedSocketAddress::SetLocal(const char *path)
{
    const size_t path_length = strlen(path);

    struct sockaddr_un *sun;
    SetSize(sizeof(*sun) - sizeof(sun->sun_path) + path_length + 1);
    sun = (struct sockaddr_un *)address;
    sun->sun_family = AF_UNIX;
    memcpy(sun->sun_path, path, path_length + 1);

    if (sun->sun_path[0] == '@')
        /* abstract socket address */
        sun->sun_path[0] = 0;
}

bool
AllocatedSocketAddress::Parse(const char *p, int default_port,
                              bool passive, GError **error_r)
{
    if (*p == '/') {
        SetLocal(p);
        return true;
    }

    if (*p == '@') {
#ifdef __linux
        /* abstract unix domain socket */

        SetLocal(p);

        /* replace the '@' with a null byte to make it "abstract" */
        struct sockaddr_un *sun = (struct sockaddr_un *)address;
        assert(sun->sun_path[0] == '@');
        sun->sun_path[0] = '\0';
        return true;
#else
        /* Linux specific feature */
        g_set_error_literal(error_r, resolver_quark(), 0,
                            "Abstract sockets supported only on Linux");
        return false;
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
        return false;
    }

    SetSize(ai->ai_addrlen);
    memcpy(address, ai->ai_addr, size);

    freeaddrinfo(ai);

    return true;
}
