#include "beng-proxy/control.h"

#include <socket/resolver.h>
#include <socket/util.h>

#include <glib.h>
#include <event.h>

#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>

int main(int argc, char **argv) {
    if (argc != 2) {
        fprintf(stderr, "usage: send-udp HOST\n");
        return 1;
    }

    struct addrinfo hints, *ai;
    memset(&hints, 0, sizeof(hints));
    hints.ai_socktype = SOCK_DGRAM;

    if (socket_resolve_host_port(argv[1], 1234, &hints, &ai) != 0) {
        fprintf(stderr, "Failed to resolve host name\n");
        return 2;
    }

    int fd = socket(ai->ai_family, ai->ai_socktype, ai->ai_protocol);
    if (fd < 0) {
        perror("Failed to create socket");
        return 2;
    }

    if (connect(fd, ai->ai_addr, ai->ai_addrlen)) {
        perror("Failed to connect socket");
        return 2;
    }

    freeaddrinfo(ai);

    const struct {
        uint32_t magic;
        struct beng_control_header header;
    } packet = {
        .magic = GUINT32_TO_BE(beng_control_magic),
        .header = {
            .length = htons(0),
            .command = htons(BENG_CONTROL_NOP),
        },
    };

    ssize_t nbytes = send(fd, &packet, sizeof(packet), 0);
    if (nbytes < 0) {
        perror("Failed to send packet");
        return 2;
    }

    close(fd);
    return 0;
}
