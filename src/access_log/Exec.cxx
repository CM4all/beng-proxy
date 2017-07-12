/*
 * An access logger which binds to a UDP/datagram address and executes
 * another access logger.  It can be used to receive data from
 * "cm4all-beng-proxy-log-forward".
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "system/Error.hxx"
#include "net/UniqueSocketDescriptor.hxx"
#include "net/RBindSocket.hxx"
#include "util/PrintException.hxx"

#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <unistd.h>

int main(int argc, char **argv)
try {
    int i = 1;

    if (i + 2 > argc) {
        fprintf(stderr, "Usage: log-exec IP PROGRAM ...\n");
        return EXIT_FAILURE;
    }

    auto fd = ResolveBindDatagramSocket(argv[i++], 5479);
    fd.SetBlocking();
    fd.CheckDuplicate(FileDescriptor(STDIN_FILENO));

    execv(argv[i], &argv[i]);

    fprintf(stderr, "Failed to execute %s: %s\n",
            argv[i], strerror(errno));
    return EXIT_FAILURE;
} catch (const std::exception &e) {
    PrintException(e);
    return EXIT_FAILURE;
}
