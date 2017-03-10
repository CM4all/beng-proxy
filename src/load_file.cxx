/*
 * Load the contents of a file into a buffer.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "load_file.hxx"
#include "gerrno.h"
#include "http_quark.h"
#include "pool.hxx"
#include "io/UniqueFileDescriptor.hxx"
#include "util/ConstBuffer.hxx"

#include <http/status.h>

#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <string.h>

ConstBuffer<void>
LoadFile(struct pool &pool, const char *path, off_t max_size, GError **error_r)
{
    UniqueFileDescriptor fd;
    if (!fd.OpenReadOnly(path)) {
        int code = errno;
        g_set_error(error_r, errno_quark(), code,
                    "Failed to open %s: %s", path, strerror(code));
        return nullptr;
    }

    off_t size = fd.GetSize();
    if (size < 0) {
        int code = errno;
        g_set_error(error_r, errno_quark(), code,
                    "Failed to stat %s: %s", path, strerror(code));
        return nullptr;
    }

    if (size > max_size) {
        g_set_error(error_r, http_response_quark(),
                    HTTP_STATUS_INTERNAL_SERVER_ERROR,
                    "File is too large: %s", path);
        return nullptr;
    }

    if (size == 0)
        return { "", 0 };

    void *p = p_malloc(&pool, size);
    if (p == nullptr) {
        set_error_errno(error_r);
        return nullptr;
    }

    ssize_t nbytes = fd.Read(p, size);
    if (nbytes < 0) {
        int code = errno;
        g_set_error(error_r, errno_quark(), code,
                    "Failed to read from %s: %s", path, strerror(code));
        free(p);
        return nullptr;
    }

    if (size_t(nbytes) != size_t(size)) {
        g_set_error(error_r, http_response_quark(),
                    HTTP_STATUS_INTERNAL_SERVER_ERROR,
                    "Short read from %s", path);
        free(p);
        return nullptr;
    }

    return { p, size_t(size) };
}
