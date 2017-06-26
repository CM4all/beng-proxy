/*
 * An example server for the logging protocol.  It prints the messages
 * to stdout.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "system/Error.hxx"
#include "net/Resolver.hxx"
#include "net/AddressInfo.hxx"
#include "net/UniqueSocketDescriptor.hxx"
#include "net/SocketAddress.hxx"
#include "util/PrintException.hxx"

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>

static UniqueSocketDescriptor
open_udp(const char *host, int default_port)
{
    struct addrinfo hints;
    memset(&hints, 0, sizeof(hints));
    hints.ai_flags = AI_ADDRCONFIG|AI_PASSIVE;
    hints.ai_socktype = SOCK_DGRAM;

    const auto ail = Resolve(host, default_port, &hints);
    const auto &ai = ail.front();

    UniqueSocketDescriptor fd;
    if (!fd.Create(ai.GetFamily(), ai.GetType(), ai.GetProtocol()))
        throw MakeErrno("socket() failed");

    if (!fd.Bind(ai))
        throw MakeErrno("bind() failed");

    return fd;
}

int main(int argc, char **argv)
try {
    if (argc < 3) {
        fprintf(stderr, "Usage: log-exec IP PROGRAM ...\n");
        return EXIT_FAILURE;
    }

    auto fd = open_udp(argv[1], 5479);
    fd.CheckDuplicate(FileDescriptor(STDIN_FILENO));

    execv(argv[2], &argv[2]);

    fprintf(stderr, "Failed to execute %s: %s\n",
            argv[2], strerror(errno));
    return EXIT_FAILURE;
} catch (const std::exception &e) {
    PrintException(e);
    return EXIT_FAILURE;
}
