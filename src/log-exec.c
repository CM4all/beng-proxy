/*
 * An example server for the logging protocol.  It prints the messages
 * to stdout.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "log-server.h"

#include <socket/resolver.h>

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <netdb.h>
#include <unistd.h>
#include <sys/socket.h>

static int
open_udp(const char *host, int default_port)
{
    struct addrinfo hints;
    memset(&hints, 0, sizeof(hints));
    hints.ai_socktype = SOCK_DGRAM;
    hints.ai_flags = AI_PASSIVE;

    struct addrinfo *ai;
    int ret = socket_resolve_host_port(host, default_port, &hints, &ai);
    if (ret != 0) {
        fprintf(stderr, "Failed to resolve '%s': %s\n",
                host, gai_strerror(ret));
        return -1;
    }

    int fd = socket(ai->ai_family, ai->ai_socktype, ai->ai_protocol);
    if (fd < 0) {
        fprintf(stderr, "socket() failed: %s\n", strerror(errno));
        freeaddrinfo(ai);
        return -1;
    }

    ret = bind(fd, ai->ai_addr, ai->ai_addrlen);
    if (ret < 0) {
        fprintf(stderr, "bind() failed: %s\n", strerror(errno));
        close(fd);
        freeaddrinfo(ai);
        return -1;
    }

    freeaddrinfo(ai);
    return fd;
}

int main(int argc, char **argv)
{
    if (argc < 3) {
        fprintf(stderr, "Usage: log-exec IP PROGRAM ...\n");
        return EXIT_FAILURE;
    }

    int fd = open_udp(argv[1], 5479);
    if (fd < 0)
        return EXIT_FAILURE;

    if (fd > 0) {
        dup2(fd, 0);
        close(fd);
    }

    execv(argv[2], &argv[2]);

    fprintf(stderr, "Failed to execute %s: %s\n",
            argv[2], strerror(errno));
    return EXIT_FAILURE;
}
