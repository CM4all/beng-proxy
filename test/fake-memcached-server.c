#include "memcached-protocol.h"

#include <glib.h>

#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <netinet/in.h>

static void
read_full_or_abort(int fd, void *dest0, size_t length)
{
    char *dest = dest0;
    ssize_t nbytes;

    while (length > 0) {
        nbytes = read(fd, dest, length);
        if (nbytes < 0) {
            perror("read() failed");
            exit(2);
        }

        if (nbytes == 0)
            exit(0);

        dest += nbytes;
        length -= nbytes;
    }
}

static void
read_discard(int fd, size_t length)
{
    static char buffer[4096];

    while (length >= sizeof(buffer)) {
        read_full_or_abort(fd, buffer, sizeof(buffer));
        length -= sizeof(buffer);
    }

    read_full_or_abort(fd, buffer, length);
}

static void
write_full_or_abort(int fd, const void *dest0, size_t length)
{
    const char *dest = dest0;
    ssize_t nbytes;

    while (length > 0) {
        nbytes = write(fd, dest, length);
        if (nbytes < 0) {
            perror("write() failed");
            exit(2);
        }

        if (nbytes == 0) {
            fprintf(stderr, "empty write()\n");
            exit(2);
        }

        dest += nbytes;
        length -= nbytes;
    }
}

int main(int argc, char **argv)
{
    struct memcached_request_header request_header;
    static const char response_key[3] = "foo";
    static const char response_body1[1024];
    static const char response_body2[8192];
    const struct memcached_response_header response_header = {
        .magic = MEMCACHED_MAGIC_RESPONSE,
        .key_length = GUINT16_TO_BE(sizeof(response_key)),
        .extras_length = 0,
        .status = MEMCACHED_STATUS_NO_ERROR,
        .body_length = GUINT32_TO_BE(sizeof(response_key) +
                                     sizeof(response_body1) +
                                     sizeof(response_body2)),
        .message_id = 0,
    };

    (void)argc;
    (void)argv;

    while (true) {
        read_full_or_abort(0, &request_header, sizeof(request_header));

        if (request_header.magic != MEMCACHED_MAGIC_REQUEST) {
            fprintf(stderr, "wrong magic: 0x%02x\n", request_header.magic);
            return 2;
        }

        read_discard(0, ntohl(request_header.body_length));

        write_full_or_abort(1, &response_header, sizeof(response_header));
        write_full_or_abort(1, response_key, sizeof(response_key));
        write_full_or_abort(1, response_body1, sizeof(response_body1));
        write_full_or_abort(1, response_body2, sizeof(response_body2));
    }
}
