/*
 * Check the bulldog-tyke status directory.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "bulldog.h"

#include <daemon/log.h>
#include <socket/address.h>

#include <glib.h>

#include <assert.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>

#define WORKERS "/workers/"

static struct {
    char path[4096];

    size_t path_length;
} bulldog;

void
bulldog_init(const char *path)
{
    if (path == NULL)
        return;

    if (strlen(path) + sizeof(WORKERS) + 16 >= sizeof(bulldog.path)) {
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

gcc_pure
static const char *
bulldog_node_path(const struct sockaddr *address, socklen_t address_size,
                  const char *attribute_name)
{
    assert(address != NULL);
    assert(address_size > 0);
    assert(attribute_name != NULL);
    assert(*attribute_name != 0);

    if (bulldog.path[0] == 0)
        /* disabled */
        return NULL;

    if (!socket_address_to_string(bulldog.path + bulldog.path_length,
                                  sizeof(bulldog.path) - bulldog.path_length,
                                  address, address_size))
        return NULL;

    g_strlcat(bulldog.path, "/", sizeof(bulldog.path));
    g_strlcat(bulldog.path, attribute_name, sizeof(bulldog.path));
    return bulldog.path;
}

bool
bulldog_check(const struct sockaddr *addr, socklen_t addrlen)
{
    int fd;
    ssize_t nbytes;
    char buffer[32], *p;

    const char *path = bulldog_node_path(addr, addrlen, "status");
    if (path == NULL)
        /* disabled */
        return true;

    fd = open(path, O_RDONLY|O_CLOEXEC|O_NOCTTY);
    if (fd < 0) {
        if (errno != ENOENT)
            daemon_log(2, "Failed to open %s: %s\n",
                       path, strerror(errno));
        else
            daemon_log(4, "No such bulldog-tyke status file: %s\n",
                       path);
        return true;
    }

    nbytes = read(fd, buffer, sizeof(buffer));
    if (nbytes < 0) {
        daemon_log(2, "Failed to read %s: %s\n",
                   path, strerror(errno));
        close(fd);
        return true;
    }

    close(fd);

    /* use only the first line */
    p = memchr(buffer, '\n', nbytes);
    if (p == NULL)
        p = buffer + nbytes;

    *p = 0;

    daemon_log(5, "bulldog: %s='%s'\n", path, buffer);

    return strcmp(buffer, "alive") == 0;
}
