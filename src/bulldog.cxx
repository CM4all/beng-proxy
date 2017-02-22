/*
 * Check the bulldog-tyke status directory.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "bulldog.hxx"

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
    if (path == nullptr)
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
bulldog_deinit()
{
}

gcc_pure
static const char *
bulldog_node_path(const struct sockaddr *address, socklen_t address_size,
                  const char *attribute_name)
{
    assert(address != nullptr);
    assert(address_size > 0);
    assert(attribute_name != nullptr);
    assert(*attribute_name != 0);

    if (bulldog.path[0] == 0)
        /* disabled */
        return nullptr;

    if (!socket_address_to_string(bulldog.path + bulldog.path_length,
                                  sizeof(bulldog.path) - bulldog.path_length,
                                  address, address_size))
        return nullptr;

    g_strlcat(bulldog.path, "/", sizeof(bulldog.path));
    g_strlcat(bulldog.path, attribute_name, sizeof(bulldog.path));
    return bulldog.path;
}

gcc_pure
static const char *
read_first_line(const char *path, char *buffer, size_t buffer_size)
{
    assert(path != nullptr);
    assert(buffer != nullptr);
    assert(buffer_size > 0);

    int fd = open(path, O_RDONLY|O_CLOEXEC|O_NOCTTY);
    if (fd < 0)
        return nullptr;

    ssize_t nbytes = read(fd, buffer, buffer_size - 1);
    if (nbytes < 0)
        return nullptr;

    close(fd);

    /* use only the first line */
    char *p = (char *)memchr(buffer, '\n', nbytes);
    if (p == nullptr)
        p = buffer + nbytes;

    *p = 0;

    return buffer;
}

bool
bulldog_check(const struct sockaddr *addr, socklen_t addrlen)
{
    const char *path = bulldog_node_path(addr, addrlen, "status");
    if (path == nullptr)
        /* disabled */
        return true;

    char buffer[32];
    const char *value = read_first_line(path, buffer, sizeof(buffer));
    if (value == nullptr) {
        if (errno != ENOENT)
            daemon_log(2, "Failed to read %s: %s\n",
                       path, strerror(errno));
        else
            daemon_log(4, "No such bulldog-tyke status file: %s\n",
                       path);
        return true;
    }

    daemon_log(5, "bulldog: %s='%s'\n", path, value);

    return strcmp(value, "alive") == 0;
}

bool
bulldog_is_fading(const struct sockaddr *addr, socklen_t addrlen)
{
    const char *path = bulldog_node_path(addr, addrlen, "graceful");
    if (path == nullptr)
        /* disabled */
        return false;

    char buffer[32];
    const char *value = read_first_line(path, buffer, sizeof(buffer));
    if (value == nullptr)
        return false;

    daemon_log(5, "bulldog: %s='%s'\n", path, value);

    return strcmp(value, "1") == 0;
}
