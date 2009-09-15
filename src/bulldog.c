/*
 * Check the bulldog-tyke status directory.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "bulldog.h"

#include <daemon/log.h>
#include <socket/address.h>

#include <glib.h>

#include <string.h>
#include <stdio.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>

#define WORKERS "/workers/"
#define SUFFIX "/status"

static struct {
    char path[4096];

    size_t path_length;
} bulldog;

void
bulldog_init(const char *path)
{
    if (path == NULL)
        return;

    if (strlen(path) + sizeof(WORKERS) + sizeof(SUFFIX) >=
        sizeof(bulldog.path)) {
        daemon_log(1, "bulldog path is too long\n");
        return;
    }

    strcpy(bulldog.path, path);
    strcat(bulldog.path, WORKERS);
    bulldog.path_length = strlen(bulldog.path);
}

void
bulldog_deinit(void)
{
}

bool
bulldog_check(const struct sockaddr *addr, socklen_t addrlen)
{
    bool success;
    int fd;
    ssize_t nbytes;
    char buffer[32], *p;

    if (bulldog.path[0] == 0)
        /* disabled */
        return true;

    success = socket_address_to_string(bulldog.path + bulldog.path_length,
                                       sizeof(bulldog.path) - bulldog.path_length,
                                       addr, addrlen);
    if (!success)
        /* bail out */
        return true;

    g_strlcat(bulldog.path, SUFFIX, sizeof(bulldog.path));

    fd = open(bulldog.path, O_RDONLY|O_CLOEXEC|O_NOCTTY);
    if (fd < 0) {
        if (errno != ENOENT)
            fprintf(stderr, "Failed to open %s: %s\n",
                    bulldog.path, strerror(errno));
        return true;
    }

    nbytes = read(fd, buffer, sizeof(buffer));
    if (nbytes < 0) {
        fprintf(stderr, "Failed to read %s: %s\n",
                bulldog.path, strerror(errno));
        close(fd);
        return true;
    }

    close(fd);

    /* use only the first line */
    p = memchr(buffer, '\n', nbytes);
    if (p == NULL)
        p = buffer + nbytes;

    *p = 0;

    return strcmp(buffer, "alive") == 0;
}
