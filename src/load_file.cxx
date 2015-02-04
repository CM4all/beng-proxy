/*
 * Load the contents of a file into a buffer.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "load_file.hxx"
#include "gerrno.h"
#include "fd_util.h"
#include "http_quark.h"
#include "pool.hxx"
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
    int fd = open_cloexec(path, O_RDONLY, 0);
    if (fd < 0) {
        int code = errno;
        g_set_error(error_r, errno_quark(), code,
                    "Failed to open %s: %s", path, strerror(code));
        return nullptr;
    }

    struct stat st;
    if (fstat(fd, &st) < 0) {
        int code = errno;
        g_set_error(error_r, errno_quark(), code,
                    "Failed to stat %s: %s", path, strerror(code));
        close(fd);
        return nullptr;
    }

    if (st.st_size > max_size) {
        g_set_error(error_r, http_response_quark(),
                    HTTP_STATUS_INTERNAL_SERVER_ERROR,
                    "File is too large: %s", path);
        close(fd);
        return nullptr;
    }

    const size_t size = st.st_size;

    if (size == 0) {
        close(fd);
        return { "", 0 };
    }

    void *p = p_malloc(&pool, size);
    if (p == nullptr) {
        set_error_errno(error_r);
        close(fd);
        return nullptr;
    }

    ssize_t nbytes = read(fd, p, size);
    if (nbytes < 0) {
        int code = errno;
        g_set_error(error_r, errno_quark(), code,
                    "Failed to read from %s: %s", path, strerror(code));
        close(fd);
        free(p);
        return nullptr;
    }

    close(fd);

    if (size_t(nbytes) != size) {
        g_set_error(error_r, http_response_quark(),
                    HTTP_STATUS_INTERNAL_SERVER_ERROR,
                    "Short read from %s", path);
        free(p);
        return nullptr;
    }

    return { p, size };
}
