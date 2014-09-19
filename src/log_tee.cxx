/*
 * Launch a number of logger processes that receive a copy of all log
 * datagrams.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

extern "C" {
#include "log-launch.h"
}

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <sys/socket.h>

static constexpr unsigned MAX_CHILDREN = 32;

struct Child {
    int fd;
};

static unsigned n_children;
static Child children[MAX_CHILDREN];

static bool
Forward()
{
    char buffer[65536];
    ssize_t nbytes = recv(STDIN_FILENO, buffer, sizeof(buffer), 0);
    if (nbytes <= 0) {
        if (nbytes < 0) {
            if (errno == EAGAIN || errno == EINTR)
                return true;

            fprintf(stderr, "Failed to receive: %s\n", strerror(errno));
        }

        return false;
    }

    for (unsigned i = 0; i < n_children; ++i) {
        Child &c = children[i];
        send(c.fd, buffer, nbytes, MSG_DONTWAIT|MSG_NOSIGNAL);
    }

    return true;
}

int
main(int argc, char **argv)
{
    if (argc < 2 || unsigned(argc) > 1 + MAX_CHILDREN) {
        fprintf(stderr, "Usage: %s PROGRAM1 PROGRAM2 ...\n", argv[0]);
        return EXIT_FAILURE;
    }

    for (int i = 1; i < argc; ++i) {
        const char *program = argv[i];
        struct log_process process;

        if (log_launch(&process, program, nullptr)) {
            Child &child = children[n_children++];
            child.fd = process.fd;
        }
    }

    while (Forward()) {}
    return EXIT_SUCCESS;
}
