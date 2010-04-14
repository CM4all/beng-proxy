/*
 * Fork a process and delegate open() to it.  The subprocess returns
 * the file descriptor over a unix socket.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "delegate-protocol.h"

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

int main(int argc __attr_unused, char **argv __attr_unused)
{
    struct delegate_header header;
    ssize_t nbytes;
    char payload[4096];
    size_t length;
    int fd;

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

        fd = open(payload, O_RDONLY|O_CLOEXEC|O_NOCTTY);
        if (fd >= 0) {
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
            int ret;

            header.length = 0;
            header.command = 0;

            cmsg = CMSG_FIRSTHDR(&msg);
            cmsg->cmsg_level = SOL_SOCKET;
            cmsg->cmsg_type = SCM_RIGHTS;
            cmsg->cmsg_len = msg.msg_controllen;

            int *fd_p = (int *)CMSG_DATA(cmsg);
            *fd_p = fd;

            ret = sendmsg(0, &msg, 0);
            if (ret < 0) {
                fprintf(stderr, "failed to send fd: %s\n", strerror(errno));
                return 2;
            }

            close(fd);
        } else {
            /* error: send error code to client */

            header.length = 0;
            header.command = errno;

            nbytes = send(0, &header, sizeof(header), 0);
            if (nbytes < 0) {
                fprintf(stderr, "send() on delegate socket failed: %s\n",
                        strerror(errno));
                return 2;
            }

            if ((size_t)nbytes != sizeof(header)) {
                fprintf(stderr, "short send() on delegate socket\n");
                return 2;
            }
        }
    }
}
