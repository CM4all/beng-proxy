/*
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "load_file.hxx"
#include "HttpMessageResponse.hxx"
#include "pool.hxx"
#include "io/UniqueFileDescriptor.hxx"
#include "system/Error.hxx"
#include "util/ConstBuffer.hxx"
#include "util/StringFormat.hxx"

#include <http/status.h>

ConstBuffer<void>
LoadFile(struct pool &pool, const char *path, off_t max_size)
{
    UniqueFileDescriptor fd;
    if (!fd.OpenReadOnly(path))
        throw FormatErrno("Failed to open %s", path);

    off_t size = fd.GetSize();
    if (size < 0)
        throw FormatErrno("Failed to stat %s", path);

    if (size > max_size)
        throw HttpMessageResponse(HTTP_STATUS_INTERNAL_SERVER_ERROR,
                                  StringFormat<256>("File is too large: %s", path));

    if (size == 0)
        return { "", 0 };

    void *p = p_malloc(&pool, size);
    if (p == nullptr)
        throw std::bad_alloc();

    ssize_t nbytes = fd.Read(p, size);
    if (nbytes < 0)
        throw FormatErrno("Failed to read from %s", path);

    if (size_t(nbytes) != size_t(size))
        throw HttpMessageResponse(HTTP_STATUS_INTERNAL_SERVER_ERROR,
                                  StringFormat<256>("Short read from: %s", path));

    return { p, size_t(size) };
}
