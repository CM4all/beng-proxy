/*
 * An example server for the logging protocol.  It prints the messages
 * to stdout.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "log-server.h"
#include "net/SocketDescriptor.hxx"
#include "net/SocketAddress.hxx"
#include "net/Resolver.hxx"
#include "net/AddressInfo.hxx"
#include "util/PrintException.hxx"

#include <array>

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <sys/socket.h>

static SocketDescriptor
open_udp(const char *host, int default_port)
{
    struct addrinfo hints;
    memset(&hints, 0, sizeof(hints));
    hints.ai_flags = AI_ADDRCONFIG;
    hints.ai_socktype = SOCK_DGRAM;

    for (const auto &i : Resolve(host, default_port, &hints)) {
        SocketDescriptor fd;
        if (!fd.CreateNonBlock(i.GetFamily(), i.GetType(), i.GetProtocol())) {
            fprintf(stderr, "socket() failed: %s\n", strerror(errno));
            continue;
        }

        if (!fd.Connect(i)) {
            fprintf(stderr, "connect() failed: %s\n", strerror(errno));
            fd.Close();
            continue;
        }

        return fd;
    }

    return SocketDescriptor::Undefined();
}

struct Destination {
    SocketDescriptor fd;
    bool failed;
};

int main(int argc, char **argv)
try {
    if (argc < 2) {
        fprintf(stderr, "Usage: log-forward HOST ...\n");
        return EXIT_FAILURE;
    }

    static std::array<Destination, 256> destinations;
    unsigned num_destinations = argc - 1;

    if (num_destinations > destinations.size()) {
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
} catch (const std::exception &e) {
    PrintException(e);
    return EXIT_FAILURE;
}
