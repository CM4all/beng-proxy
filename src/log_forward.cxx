/*
 * An example server for the logging protocol.  It prints the messages
 * to stdout.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "log-server.h"
#include "net/SocketDescriptor.hxx"
#include "net/SocketAddress.hxx"
#include "util/Macros.hxx"

#include <socket/resolver.h>

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <netdb.h>
#include <unistd.h>
#include <sys/socket.h>

static SocketDescriptor
open_udp(const char *host, int default_port)
{
    struct addrinfo hints;
    memset(&hints, 0, sizeof(hints));
    hints.ai_flags = AI_ADDRCONFIG;
    hints.ai_socktype = SOCK_DGRAM;

    struct addrinfo *ai;
    int ret = socket_resolve_host_port(host, default_port, &hints, &ai);
    if (ret != 0) {
        fprintf(stderr, "Failed to resolve '%s': %s\n",
                host, gai_strerror(ret));
        return SocketDescriptor::Undefined();
    }

    for (const struct addrinfo *i = ai; i != nullptr; i = i->ai_next) {
        SocketDescriptor fd;
        if (!fd.CreateNonBlock(i->ai_family, i->ai_socktype, i->ai_protocol)) {
            fprintf(stderr, "socket() failed: %s\n", strerror(errno));
            continue;
        }

        if (!fd.Connect({i->ai_addr, i->ai_addrlen})) {
            fprintf(stderr, "connect() failed: %s\n", strerror(errno));
            fd.Close();
            continue;
        }

        freeaddrinfo(ai);
        return fd;
    }

    freeaddrinfo(ai);
    return SocketDescriptor::Undefined();
}

struct Destination {
    SocketDescriptor fd;
    bool failed;
};

int main(int argc, char **argv)
{
    if (argc < 2) {
        fprintf(stderr, "Usage: log-forward HOST ...\n");
        return EXIT_FAILURE;
    }

    static Destination destinations[256];
    unsigned num_destinations = argc - 1;

    if (num_destinations > ARRAY_SIZE(destinations)) {
        fprintf(stderr, "Too many hosts\n");
        return EXIT_FAILURE;
    }

    for (unsigned i = 0; i < num_destinations; ++i) {
        destinations[i].fd = open_udp(argv[1 + i], 5479);
        if (!destinations[i].fd.IsDefined())
            return EXIT_FAILURE;
    }

    static char buffer[16384];
    ssize_t nbytes;
    while ((nbytes = recv(0, buffer, sizeof(buffer), 0)) > 0) {
        size_t length = (size_t)nbytes;
        for (unsigned i = 0; i < num_destinations; ++i) {
            nbytes = destinations[i].fd.Write(buffer, length);
            if (nbytes == (ssize_t)length)
                destinations[i].failed = false;
            else if (nbytes < 0 && errno != EAGAIN && errno != EWOULDBLOCK &&
                     !destinations[i].failed) {
                fprintf(stderr, "send() to host %u failed: %s\n",
                        i, strerror(errno));
                destinations[i].failed = true;
            }
        }
    }

    return 0;
}
