/*
 * Serve HTTP requests from the local VFS.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "handler.h"
#include "request.h"
#include "connection.h"
#include "header-writer.h"
#include "processor.h"
#include "date.h"
#include "format.h"
#include "embed.h"
#include "session.h"

#include <assert.h>
#include <sys/stat.h>
#include <errno.h>
#include <string.h>

#ifndef NO_XATTR
#include <attr/xattr.h>
#endif

static void
make_etag(char *p, const struct stat *st)
{
    *p++ = '"';

    p += format_uint32_hex(p, (uint32_t)st->st_dev);

    *p++ = '-';

    p += format_uint32_hex(p, (uint32_t)st->st_ino);

    *p++ = '-';

    p += format_uint32_hex(p, (uint32_t)st->st_mtime);

    *p++ = '"';
    *p = 0;
}

void
file_callback(struct request *request2)
{
    struct http_server_request *request = request2->request;
    const struct translate_response *tr = request2->translate.response;
    const char *path;
    int ret;
    growing_buffer_t headers;
    istream_t body;
    struct stat st;
    char buffer[64];
    http_status_t status;

    assert(tr != NULL);
    assert(tr->path != NULL);

    path = tr->path;

    if (request->method != HTTP_METHOD_HEAD &&
        request->method != HTTP_METHOD_GET &&
        request->method != HTTP_METHOD_POST) {
        http_server_send_message(request,
                                 HTTP_STATUS_METHOD_NOT_ALLOWED,
                                 "This method is not allowed.");
        return;
    }

    ret = lstat(path, &st);
    if (ret != 0) {
        if (errno == ENOENT) {
            http_server_send_message(request,
                                     HTTP_STATUS_NOT_FOUND,
                                     "The requested file does not exist.");
        } else {
            http_server_send_message(request,
                                     HTTP_STATUS_INTERNAL_SERVER_ERROR,
                                     "Internal server error");
        }
        return;
    }

    if (!S_ISREG(st.st_mode)) {
        http_server_send_message(request,
                                 HTTP_STATUS_INTERNAL_SERVER_ERROR,
                                 "Not a regular file");
        return;
    }

    body = istream_file_new(request->pool, path, st.st_size);
    if (body == NULL) {
        if (errno == ENOENT) {
            http_server_send_message(request,
                                     HTTP_STATUS_NOT_FOUND,
                                     "The requested file does not exist.");
        } else {
            http_server_send_message(request,
                                     HTTP_STATUS_INTERNAL_SERVER_ERROR,
                                     "Internal server error");
        }
        return;
    }

    headers = growing_buffer_new(request->pool, 2048);

    make_etag(buffer, &st);
    header_write(headers, "etag", buffer);
 
    if (tr->content_type != NULL) {
        header_write(headers, "content-type", tr->content_type);
    } else {
#ifndef NO_XATTR
        ssize_t nbytes;
        char content_type[256];

        nbytes = getxattr(path, "user.Content-Type", /* XXX use fgetxattr() */
                          content_type, sizeof(content_type) - 1);
        if (nbytes > 0) {
            assert((size_t)nbytes < sizeof(content_type));
            content_type[nbytes] = 0;
            header_write(headers, "content-type", content_type);
        } else {
#endif /* #ifndef NO_XATTR */
            header_write(headers, "content-type", "application/octet-stream");
#ifndef NO_XATTR
        }
#endif /* #ifndef NO_XATTR */
    }

    status = tr->status == 0 ? HTTP_STATUS_OK : tr->status;

    if (!tr->process) {
        if (request->method == HTTP_METHOD_POST) {
            istream_close(body);
            http_server_send_message(request,
                                     HTTP_STATUS_METHOD_NOT_ALLOWED,
                                     "This method is not allowed.");
            return;
        }

#ifndef NO_LAST_MODIFIED_HEADER
        header_write(headers, "last-modified", http_date_format(st.st_mtime));
#endif

        if (request->body != NULL)
            istream_close(request->body);
    }

    response_dispatch(request2, status, headers, body);
}
