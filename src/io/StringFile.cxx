/*
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "StringFile.hxx"
#include "system/Error.hxx"
#include "io/UniqueFileDescriptor.hxx"
#include "util/StringUtil.hxx"

std::string
LoadStringFile(const char *path)
{
    UniqueFileDescriptor fd;
    if (!fd.OpenReadOnly(path))
        throw FormatErrno("Failed to open %s", path);

    char buffer[1024];
    ssize_t nbytes = fd.Read(buffer, sizeof(buffer));
    if (nbytes < 0)
        throw FormatErrno("Failed to read %s", path);

    size_t length = StripRight(buffer, nbytes);
    if (length >= sizeof(buffer))
        throw std::runtime_error("File is too large: " + std::string(path));

    buffer[length] = 0;
    return StripLeft(buffer);
}
