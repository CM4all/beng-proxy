/*
 * Fork a process and delegate open() to it.  The subprocess returns
 * the file descriptor over a unix socket.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "delegate_protocol.h"

#include <inline/compiler.h>

#include <stdbool.h>
#include <sys/socket.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/un.h>
#include <unistd.h>

#ifndef O_CLOEXEC
enum {
    O_CLOEXEC = 0,
};
#endif

static bool
delegate_send(const void *data, size_t length)
{
    ssize_t nbytes = send(0, data, length, 0);
    if (nbytes < 0) {
        fprintf(stderr, "send() on delegate socket failed: %s\n",
                strerror(errno));
        return false;
    }

    if ((size_t)nbytes != length) {
        fprintf(stderr, "short send() on delegate socket\n");
        return false;
    }

    return true;
}

static bool
delegate_send_int(enum delegate_response_command command, int value)
{
    const struct delegate_packet_int packet = {
        .header = {
            .length = sizeof(packet) - sizeof(packet.header),
            .command = (uint16_t)command,
        },
        .value = value,
    };

    return delegate_send(&packet, sizeof(packet));
}

static bool
delegate_send_fd(enum delegate_response_command command, int fd)
{
    struct delegate_header header = {
        .length = 0,
        .command = (uint16_t)command,
    };
    struct iovec vec = {
        .iov_base = &header,
        .iov_len = sizeof(header),
    };

    char ccmsg[CMSG_SPACE(sizeof(fd))];
    struct msghdr msg = {
        .msg_name = NULL,
        .msg_namelen = 0,
        .msg_iov = &vec,
        .msg_iovlen = 1,
        .msg_control = ccmsg,
        .msg_controllen = CMSG_LEN(sizeof(fd)),
        .msg_flags = 0,
    };
    struct cmsghdr *cmsg;

    cmsg = CMSG_FIRSTHDR(&msg);
    cmsg->cmsg_level = SOL_SOCKET;
    cmsg->cmsg_type = SCM_RIGHTS;
    cmsg->cmsg_len = msg.msg_controllen;

#if GCC_CHECK_VERSION(4,6)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wstrict-aliasing"
#endif

    *(int *)CMSG_DATA(cmsg) = fd;

#if GCC_CHECK_VERSION(4,6)
#pragma GCC diagnostic pop
#endif

    if (sendmsg(0, &msg, 0) < 0) {
        fprintf(stderr, "failed to send fd: %s\n", strerror(errno));
        return false;
    }

    return true;
}

static bool
delegate_handle_open(const char *payload)
{
    int fd = open(payload, O_RDONLY|O_CLOEXEC|O_NOCTTY);
    if (fd >= 0) {
        bool success = delegate_send_fd(DELEGATE_FD, fd);
        close(fd);
        return success;
    } else {
        /* error: send error code to client */

        return delegate_send_int(DELEGATE_ERRNO, errno);
    }
}

static bool
delegate_handle(enum delegate_request_command command,
                const char *payload, size_t length)
{
    (void)length;

    switch (command) {
    case DELEGATE_OPEN:
        return delegate_handle_open(payload);
    }

    fprintf(stderr, "unknown command: %d\n", command);
    return false;
}

int main(int argc gcc_unused, char **argv gcc_unused)
{
    struct delegate_header header;
    ssize_t nbytes;
    char payload[4096];
    size_t length;

    while (true) {
        nbytes = recv(0, &header, sizeof(header), 0);
        if (nbytes < 0) {
            fprintf(stderr, "recv() on delegate socket failed: %s\n",
                    strerror(errno));
            return 2;
        }

        if (nbytes == 0)
            break;

        if ((size_t)nbytes != sizeof(header)) {
            fprintf(stderr, "short recv() on delegate socket\n");
            return 2;
        }

        if (header.length >= sizeof(payload)) {
            fprintf(stderr, "delegate payload too large\n");
            return 2;
        }

        length = 0;

        while (length < header.length) {
            nbytes = recv(0, payload + length,
                          sizeof(payload) - 1 - length, 0);
            if (nbytes < 0) {
                fprintf(stderr, "recv() on delegate socket failed: %s\n",
                        strerror(errno));
                return 2;
            }

            if (nbytes == 0)
                break;

            length += (size_t)nbytes;
        }

        payload[length] = 0;

        if (!delegate_handle((enum delegate_request_command)header.command, payload, length))
            return 2;
    }

    return 0;
}
