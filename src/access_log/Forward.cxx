/*
 * Copyright 2007-2017 Content Management AG
 * All rights reserved.
 *
 * author: Max Kellermann <mk@cm4all.com>
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * - Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 *
 * - Redistributions in binary form must reproduce the above copyright
 * notice, this list of conditions and the following disclaimer in the
 * documentation and/or other materials provided with the
 * distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE
 * FOUNDATION OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED
 * OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/*
 * An access logger which forwards all datagrams to one or more remote
 * loggers via UDP or a local datagram socket.
 */

#include "net/UniqueSocketDescriptor.hxx"
#include "net/RConnectSocket.hxx"
#include "util/PrintException.hxx"

#include <array>

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <sys/socket.h>

struct Destination {
    UniqueSocketDescriptor fd;
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
        destinations[i].fd = ResolveConnectDatagramSocket(argv[1 + i], 5479);
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
