/*
 * I/O utilities for unit tests.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include <sys/types.h>
#include <sys/socket.h>
#include <stdlib.h>
#include <stdint.h>
#include <netinet/in.h>

static void
read_full(void *_p, size_t length)
{
    uint8_t *p = _p, *const end = p + length;

    while (p < end) {
        ssize_t nbytes = recv(0, p, length, MSG_WAITALL);
        if (nbytes <= 0)
            exit(EXIT_FAILURE);
        p += nbytes;
    }
}

gcc_unused
static uint8_t
read_byte(size_t *remaining_r)
{
    uint8_t value;

    if (*remaining_r < sizeof(value))
        exit(EXIT_FAILURE);

    read_full(&value, sizeof(value));
    (*remaining_r) -= sizeof(value);
    return value;
}

gcc_unused
static uint16_t
read_short(size_t *remaining_r)
{
    uint16_t value;

    if (*remaining_r < sizeof(value))
        exit(EXIT_FAILURE);

    read_full(&value, sizeof(value));
    (*remaining_r) -= sizeof(value);
    return ntohs(value);
}

gcc_unused
static void
discard(size_t length)
{
    while (length > 0) {
        uint8_t buffer[1024];
        size_t nbytes = length;
        if (nbytes > sizeof(buffer))
            nbytes = sizeof(buffer);
        read_full(buffer, nbytes);
        length -= nbytes;
    }
}

static void
write_full(const void *_p, size_t length)
{
    const uint8_t *p = _p, *const end = p + length;

    while (p < end) {
        ssize_t nbytes = send(0, p, length, MSG_NOSIGNAL);
        if (nbytes <= 0)
            exit(EXIT_FAILURE);
        p += nbytes;
    }
}

gcc_unused
static void
write_byte(const uint8_t value)
{
    write_full(&value, sizeof(value));
}

gcc_unused
static void
write_short(uint16_t value)
{
    const uint16_t buffer = htons(value);
    write_full(&buffer, sizeof(buffer));
}

gcc_unused
static void
fill(size_t length)
{
    while (length > 0) {
        static const uint8_t buffer[1024];
        size_t nbytes = length;
        if (nbytes > sizeof(buffer))
            nbytes = sizeof(buffer);
        write_full(buffer, nbytes);
        length -= nbytes;
    }
}
