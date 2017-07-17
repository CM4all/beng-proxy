/*
 * Check the bulldog-tyke status directory.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "bulldog.hxx"
#include "net/SocketAddress.hxx"
#include "net/ToString.hxx"
#include "io/Logger.hxx"
#include "util/StringBuilder.hxx"

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
try {
    if (path == nullptr)
        return;

    StringBuilder<> b(bulldog.path, sizeof(bulldog.path));
    b.Append(path);
    b.Append(WORKERS);

    bulldog.path_length = strlen(bulldog.path);
} catch (StringBuilder<>::Overflow) {
    bulldog.path[0] = 0;
    LogConcat(1, "bulldog", "bulldog path is too long");
}

void
bulldog_deinit()
{
}

gcc_pure
static const char *
bulldog_node_path(SocketAddress address,
                  const char *attribute_name)
try {
    assert(!address.IsNull());
    assert(attribute_name != nullptr);
    assert(*attribute_name != 0);

    if (bulldog.path[0] == 0)
        /* disabled */
        return nullptr;

    if (!ToString(bulldog.path + bulldog.path_length,
                  sizeof(bulldog.path) - bulldog.path_length,
                  address))
        return nullptr;

    StringBuilder<> b(bulldog.path + strlen(bulldog.path),
                      bulldog.path + sizeof(bulldog.path));
    b.Append('/');
    b.Append(attribute_name);
    return bulldog.path;
} catch (StringBuilder<>::Overflow) {
    return nullptr;
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
bulldog_check(SocketAddress address)
{
    const char *path = bulldog_node_path(address, "status");
    if (path == nullptr)
        /* disabled */
        return true;

    char buffer[32];
    const char *value = read_first_line(path, buffer, sizeof(buffer));
    if (value == nullptr) {
        if (errno != ENOENT)
            LogConcat(2, "bulldog", "Failed to read ",
                      path, ": ", strerror(errno));
        else
            LogConcat(4, "bulldog", "No such bulldog-tyke status file: ",
                      path);
        return true;
    }

    LogFormat(5, "bulldog", "%s='%s'", path, value);

    return strcmp(value, "alive") == 0;
}

bool
bulldog_is_fading(SocketAddress address)
{
    const char *path = bulldog_node_path(address, "graceful");
    if (path == nullptr)
        /* disabled */
        return false;

    char buffer[32];
    const char *value = read_first_line(path, buffer, sizeof(buffer));
    if (value == nullptr)
        return false;

    LogFormat(5, "bulldog", "%s='%s'", path, value);

    return strcmp(value, "1") == 0;
}
