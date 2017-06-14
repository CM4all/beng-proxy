#include "beng-proxy/control.h"
#include "net/UniqueSocketDescriptor.hxx"
#include "net/RConnectSocket.hxx"
#include "util/ByteOrder.hxx"

#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <arpa/inet.h>

int main(int argc, char **argv) {
    if (argc != 2) {
        fprintf(stderr, "usage: send-udp HOST\n");
        return 1;
    }

    UniqueSocketDescriptor s = ResolveConnectDatagramSocket(argv[1], 1234);

    static constexpr struct {
        uint32_t magic;
        struct beng_control_header header;
    } packet = {
        .magic = ToBE32(control_magic),
        .header = {
            .length = ToBE16(0),
            .command = ToBE16(CONTROL_NOP),
        },
    };

    ssize_t nbytes = send(s.Get(), &packet, sizeof(packet), 0);
    if (nbytes < 0) {
        perror("Failed to send packet");
        return 2;
    }

    return 0;
}
