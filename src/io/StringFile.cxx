/*
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "StringFile.hxx"
#include "system/Error.hxx"
#include "util/StringUtil.hxx"
#include "util/ScopeExit.hxx"

#include <unistd.h>
#include <fcntl.h>

std::string
LoadStringFile(const char *path)
{
    int fd = open(path, O_RDONLY|O_CLOEXEC|O_NOCTTY);
    if (fd < 0)
        throw FormatErrno("Failed to open %s", path);

    AtScopeExit(fd) { close(fd); };

    char buffer[1024];
    ssize_t nbytes = read(fd, buffer, sizeof(buffer));
    if (nbytes < 0)
        throw FormatErrno("Failed to read %s", path);

    size_t length = StripRight(buffer, nbytes);
    if (length >= sizeof(buffer))
        throw std::runtime_error("File is too large: " + std::string(path));

    buffer[length] = 0;
    return StripLeft(buffer);
}
